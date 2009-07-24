/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2009 John Tsiombikas <nuclear@member.fsf.org>

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

#ifndef DEV_H_
#define DEV_H_

#include "config.h"
#include "event.h"

/* device hotplug detection */
int init_hotplug(void);
void shutdown_hotplug(void);
int get_hotplug_fd(void);

int handle_hotplug(void);


/* device handling */
int init_dev(void);
void shutdown_dev(void);
int get_dev_fd(void);

int read_dev(struct dev_input *inp);

void set_led(int state);

#endif	/* DEV_H_ */
