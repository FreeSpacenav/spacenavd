/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2025 John Tsiombikas <nuclear@mutantstargoat.com>

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
#ifndef SPNAV_DEV_H_
#define SPNAV_DEV_H_

#include <limits.h>
#include "config.h"

struct dev_input;

#define MAX_DEV_NAME	256

struct device {
	int id;
	int fd;
	void *data;
	char name[MAX_DEV_NAME];
	char path[PATH_MAX];
	int type;
	unsigned int usbid[2];	/* vendor:product for USB devices */
	unsigned int flags;

	int num_axes, num_buttons;
	int bnbase;				/* button base (reported number of first button) */
	int *minval, *maxval;	/* input value range (default: -500, 500) */
	int *fuzz;				/* noise threshold */

	void (*close)(struct device*);
	int (*read)(struct device*, struct dev_input*);
	void (*set_led)(struct device*, int);

	int (*bnhack)(int bn);

	struct device *next;
};

void init_devices(void);
void init_devices_serial(void);
int init_devices_usb(void);

void remove_device(struct device *dev);

int get_device_fd(struct device *dev);
#define is_device_valid(dev) (get_device_fd(dev) >= 0)
int get_device_index(struct device *dev);
int read_device(struct device *dev, struct dev_input *inp);
void set_device_led(struct device *dev, int state);
void set_devices_led(int state);

struct device *get_devices(void);

struct device *dev_path_in_use(const char *dev_path);

#endif	/* SPNAV_DEV_H_ */
