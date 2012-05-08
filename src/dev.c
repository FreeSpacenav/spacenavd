/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2012 John Tsiombikas <nuclear@member.fsf.org>

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
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "dev.h"
#include "dev_usb.h"
#include "dev_serial.h"
#include "spnavd.h"

static struct device dev = {-1, 0};

int init_dev(void)
{
	if(dev.fd != -1) {
		fprintf(stderr, "init_dev called, but device is already open\n");
		return -1;
	}

	if(cfg.serial_dev[0]) {
		/* try to open a serial device if specified in the config file */
		printf("using device: %s\n", cfg.serial_dev);

		if(open_dev_serial(&dev, cfg.serial_dev) == -1) {
			return -1;
		}
	} else {
		const char *dev_path;

		if(!(dev_path = find_usb_device())) {
			fprintf(stderr, "failed to find the spaceball device file\n");
			return -1;
		}
		printf("using device: %s\n", dev_path);

		if(open_dev_usb(&dev, dev_path) == -1) {
			return -1;
		}
	}

	return 0;
}

void shutdown_dev(void)
{
	if(dev.close) {
		dev.close(&dev);
	}
}

int get_dev_fd(void)
{
	return dev.fd;
}

int read_dev(struct dev_input *inp)
{
	if(!dev.read) {
		return -1;
	}
	return dev.read(&dev, inp);
}

void set_led(int state)
{
	if(dev.set_led) {
		dev.set_led(&dev, state);
	}
}
