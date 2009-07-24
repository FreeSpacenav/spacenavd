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

#ifndef EVENT_H_
#define EVENT_H_

#include "config.h"
#include <sys/time.h>

enum {
	EVENT_MOTION,
	EVENT_BUTTON	/* includes both press and release */
};

struct event_motion {
	int type;
	int x, y, z;
	int rx, ry, rz;
	unsigned int period;
	int *data;
};

struct event_button {
	int type;
	int press;
	int bnum;
};

typedef union spnav_event {
	int type;
	struct event_motion motion;
	struct event_button button;
} spnav_event;



enum {
	INP_MOTION,
	INP_BUTTON,
	INP_FLUSH
};

struct dev_input {
	int type;
	struct timeval tm;
	int idx;
	int val;
};


void process_input(struct dev_input *inp);


#endif	/* EVENT_H_ */
