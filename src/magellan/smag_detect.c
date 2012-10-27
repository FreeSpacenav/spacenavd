/*
serial magellan device support for spacenavd

Copyright (C) 2012 John Tsiombikas <nuclear@member.fsf.org>
Copyright (C) 2010 Thomas Anderson <ta@nextgenengineering.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "magellan/smag.h"
#include "magellan/smag_detect.h"
#include "magellan/serialconstants.h"
#include "magellan/smag_comm.h"

/*swap out /r for /n for string printing*/
static void make_printable(char *str)
{
	while(*str) {
		if(*str == '\r') {
			*str = '\n';
		}
		str++;
	}
}

int smag_detect(const char *fname, char *buf, int sz)
{
	int fd, bytesrd, pos;
	char tmpbuf[MAXREADSIZE];

	if((fd = smag_open_device(fname)) == -1) {
		fprintf(stderr, "%s: couldn't open device file: %s\n", __func__, fname);
		return -1;
	}
	if(smag_set_port_spaceball(fd) == -1) {
		close(fd);
		fprintf(stderr, "%s: couldn't setup port\n", __func__);
		return -1;
	}

	/* first look for spaceball. should have data after open and port setup.
	 * I was hoping that using the select inside serialWaitRead would allow me
	 * to get rid of the following sleep. Removing the sleep causes port to freeze.
	 */
	sleep(1);

	bytesrd = 0;
	pos = 0;

	while((pos = smag_wait_read(fd, tmpbuf + bytesrd, MAXREADSIZE - bytesrd, 1)) > 0) {
		bytesrd += pos;
	}
	if(bytesrd > 0) {
		smag_write(fd, "hm", 2);
		while((pos = smag_wait_read(fd, tmpbuf + bytesrd, MAXREADSIZE - bytesrd, 1)) > 0) {
			bytesrd += pos;
		}

		smag_write(fd, "\"", 1);
		while((pos = smag_wait_read(fd, tmpbuf + bytesrd, MAXREADSIZE - bytesrd, 1)) > 0) {
			bytesrd += pos;
		}

		make_printable(tmpbuf);
		strncpy(buf, tmpbuf, sz);
		if(bytesrd < sz) {
			fprintf(stderr, "%s: buffer overrun\n", __func__);
			return -1;
		}
	}

	/*now if we are here we don't have a spaceball and now we need to check for a magellan */
	close(fd);
	pos = 0;

	if((fd = smag_open_device(fname)) == -1) {
		return -1;
	}
	if(smag_set_port_magellan(fd) == -1) {
		return -1;
	}
	sleep(1);

	smag_init_device(fd);
	get_version_string(fd, tmpbuf, MAXREADSIZE);

	make_printable(tmpbuf);
	strncpy(buf, tmpbuf, sz);
	close(fd);
	return 0;
}
