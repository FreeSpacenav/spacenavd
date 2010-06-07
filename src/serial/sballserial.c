/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2010 John Tsiombikas <nuclear@member.fsf.org>

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

This file incorporates work covered by the following copyright and
permission notice:

   Copyright 1997-2001 John E. Stone (j.stone@acm.org)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
   3. The name of the author may not be used to endorse or promote products
      derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
   EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
   OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
   IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
   OF SUCH DAMAGE.
*/

#define _POSIX_SOURCE 1

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "sballserial.h"	/* protos and types for this file */

typedef struct {
	int fd;			/* serial port device file descriptor */
} commstruct;

int sball_comm_open(const char *commname, SBallCommHandle * commhandleptr)
{
	struct termios sballtermio;

	commstruct *comm;

	*commhandleptr = NULL;

	comm = malloc(sizeof(commstruct));
	if(comm == NULL)
		return -1;

	comm->fd = open(commname, O_RDWR | O_NONBLOCK | O_NOCTTY);

	if(comm->fd == -1) {
		free(comm);
		return -1;	/* failed open of comm port */
	}
	tcgetattr(comm->fd, &sballtermio);

#if 0
	/* TIOCEXCL exclusive access by this process */
#if defined(TIOCEXCL)
	if(ioctl(comm->fd, TIOCEXCL) < 0) {
		return -1;	/* couldn't get exclusive use of port */
	}
#endif
#endif

	sballtermio.c_lflag = 0;
	sballtermio.c_lflag = 0;
	sballtermio.c_iflag = IGNBRK | IGNPAR;
	sballtermio.c_oflag = 0;
	sballtermio.c_cflag = CREAD | CS8 | CLOCAL | HUPCL;
	sballtermio.c_cc[VEOL] = '\r';
	sballtermio.c_cc[VERASE] = 0;
	sballtermio.c_cc[VKILL] = 0;
	sballtermio.c_cc[VMIN] = 0;
	sballtermio.c_cc[VTIME] = 0;

	/* use of baud rate in cflag is deprecated according to the */
	/* single unix spec, also doesn't work in IRIX > 6.2        */
	cfsetispeed(&sballtermio, B9600);
	cfsetospeed(&sballtermio, B9600);

	tcsetattr(comm->fd, TCSAFLUSH, &sballtermio);

	*commhandleptr = (SBallCommHandle) comm;

	return 0;
}

int sball_comm_write(SBallCommHandle commhandle, const char *buf)
{
	commstruct *comm = (commstruct *) commhandle;

	if(comm == NULL)
		return -1;

	return write(comm->fd, buf, strlen(buf));
}

int sball_comm_read(SBallCommHandle commhandle, char *buf, int sz)
{
	commstruct *comm = (commstruct *) commhandle;

	if(comm == NULL)
		return -1;

	return read(comm->fd, buf, sz);
}

int sball_comm_close(SBallCommHandle * commhandleptr)
{
	commstruct *comm = (commstruct *) (*commhandleptr);

	if(comm == NULL)
		return -1;

	close(comm->fd);

	free(*commhandleptr);
	*commhandleptr = NULL;

	return 0;
}

int sball_comm_fd(SBallCommHandle commhandle)
{
	return ((commstruct *) commhandle)->fd;
}

/* end of unix code */
