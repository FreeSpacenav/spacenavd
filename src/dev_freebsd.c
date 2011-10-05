/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2011 John Tsiombikas <nuclear@member.fsf.org>

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

#ifdef __FreeBSD__

#include "config.h"
#include <stdio.h>
#include "dev.h"
#include "cfgfile.h"
#include "spnavd.h"
#include "dev_serial.h"

static int dev_fd = -1;
static int dev_is_serial;

int init_hotplug(void)
{
	return -1;
}

void shutdown_hotplug(void)
{
}

int get_hotplug_fd(void)
{
	return -1;
}

int handle_hotplug(void)
{
	return -1;
}

int init_dev(void)
{
	if(cfg.serial_dev[0]) {
		/* try to open a serial device if specified in the config file */
		printf("using device: %s\n", cfg.serial_dev);

		if((dev_fd = open_dev_serial(cfg.serial_dev)) == -1) {
			return -1;
		}
		dev_is_serial = 1;

	} else {
		fprintf(stderr, "USB devices aren't currently supported on this platform\n");
		return -1;
	}

	return 0;
}

void shutdown_dev(void)
{
	if(dev_is_serial) {
		close_dev_serial();
	} else {
		/* TODO */
	}
}

int get_dev_fd(void)
{
	return dev_fd;
}

int read_dev(struct dev_input *inp)
{
	return dev_is_serial ? read_dev_serial(inp) : -1;
}

void set_led(int state)
{
}

#endif	/* __FreeBSD__ */
