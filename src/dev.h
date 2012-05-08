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

#include "config.h"

struct dev_input;

#define MAX_DEV_NAME	256

struct device {
	int fd;
	void *data;
	char name[MAX_DEV_NAME];

	void (*close)(struct device*);
	int (*read)(struct device*, struct dev_input*);
	void (*set_led)(struct device*, int);
};

int init_dev(void);
void shutdown_dev(void);
int get_dev_fd(void);
#define is_dev_valid()	(get_dev_fd() >= 0)

int read_dev(struct dev_input *inp);

void set_led(int state);

#endif	/* SPNAV_DEV_H_ */
