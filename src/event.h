/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2022 John Tsiombikas <nuclear@member.fsf.org>

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
#include "dev.h"

enum {
	EVENT_MOTION,
	EVENT_BUTTON,	/* includes both press and release */

	/* protocol v1 events */
	EVENT_DEV,		/* device change */
	EVENT_CFG		/* configuration change */
};

enum { DEV_ADD, DEV_RM };

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

struct event_dev {
	int type;
	int op;
	int id;
	int devtype;
	int usbid[2];
};

struct event_cfg {
	int type;
	int cfg;
	int data[6];
};

typedef union spnav_event {
	int type;
	struct event_motion motion;
	struct event_button button;
	struct event_dev dev;
	struct event_cfg cfg;
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

void remove_dev_event(struct device *dev);

void process_input(struct device *dev, struct dev_input *inp);

/* non-zero if the last processed motion event was in the deadzone */
int in_deadzone(struct device *dev);

/* dispatches the last event */
void repeat_last_event(struct device *dev);

/* broadcasts an event to all clients */
void broadcast_event(spnav_event *ev);

#endif	/* EVENT_H_ */
