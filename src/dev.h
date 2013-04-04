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
#ifndef SPNAV_DEV_H_
#define SPNAV_DEV_H_

#include <limits.h>
#include "config.h"

struct dev_input;

#define MAX_DEV_NAME	256

struct device {
	int fd;
	void *data;
	char name[MAX_DEV_NAME];
  char path[PATH_MAX];

	void (*close)(struct device*);
	int (*read)(struct device*, struct dev_input*);
	void (*set_led)(struct device*, int);

  struct device *next;
};

int init_devices(void);

void remove_device(struct device *dev);

int get_device_fd(struct device *dev);
#define is_device_valid(dev) (get_device_fd(dev) >= 0)
int get_device_index(struct device *dev);
int read_device(struct device *dev, struct dev_input *inp);
void set_device_led(struct device *dev, int state);

struct device *first_device(void);
struct device *next_device(void);

#endif	/* SPNAV_DEV_H_ */
