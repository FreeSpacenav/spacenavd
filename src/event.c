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
#include <stdlib.h>
#include "event.h"
#include "client.h"
#include "proto_unix.h"
#include "spnavd.h"

#ifdef USE_X11
#include "proto_x11.h"
#include "kbemu.h"
#endif

enum {
	MOT_X, MOT_Y, MOT_Z,
	MOT_RX, MOT_RY, MOT_RZ
};

static void dispatch_event(spnav_event *ev);
static void send_event(spnav_event *ev, struct client *c);
static unsigned int msec_dif(struct timeval tv1, struct timeval tv2);

static spnav_event ev;
static int ev_pending;

/* process_input processes an device input event, and dispatches
 * spacenav events to the clients by calling dispatch_event.
 * relative inputs (INP_MOTION) are accumulated, and dispatched when
 * we get an INP_FLUSH event. Button events are dispatched immediately
 * and they implicitly flush any pending motion event.
 */
void process_input(struct dev_input *inp)
{
	int sign;

	switch(inp->type) {
	case INP_MOTION:
		if(abs(inp->val) < cfg.dead_threshold[inp->idx] ) {
			inp->val = 0;
		}

		inp->idx = cfg.map_axis[inp->idx];
		sign = cfg.invert[inp->idx] ? -1 : 1;

		inp->val = (int)((float)inp->val * cfg.sensitivity * (inp->idx < 3 ? cfg.sens_trans[inp->idx] : cfg.sens_rot[inp->idx - 3]));

		ev.type = EVENT_MOTION;
		ev.motion.data = (int*)&ev.motion.x;
		ev.motion.data[inp->idx] = sign * inp->val;
		ev_pending = 1;
		break;

	case INP_BUTTON:
#ifdef USE_X11
		/* check to see if we must emulate a keyboard event instead of a
		 * retular button event for this button
		 */
		if(cfg.kbmap_str[inp->idx]) {
			if(!cfg.kbmap[inp->idx]) {
				cfg.kbmap[inp->idx] = kbemu_keysym(cfg.kbmap_str[inp->idx]);
				printf("mapping ``%s'' to keysym %d\n", cfg.kbmap_str[inp->idx], (int)cfg.kbmap[inp->idx]);
			}
			send_kbevent(cfg.kbmap[inp->idx], inp->val);
			break;
		}
#endif

		if(ev_pending) {
			dispatch_event(&ev);
			ev_pending = 0;
		}
		inp->idx = cfg.map_button[inp->idx];

		{
			union spnav_event bev;
			bev.type = EVENT_BUTTON;
			bev.button.press = inp->val;
			bev.button.bnum = inp->idx;
			dispatch_event(&bev);
		}
		break;

	case INP_FLUSH:
		if(ev_pending) {
			dispatch_event(&ev);
			ev_pending = 0;
		}
		break;

	default:
		break;
	}
}

int in_deadzone(void)
{
	int i;
	if(!ev.motion.data) {
		ev.motion.data = &ev.motion.x;
	}

	for(i=0; i<6; i++) {
		if(ev.motion.data[i] != 0) {
			return 0;
		}
	}
	return 1;
}

void repeat_last_event(void)
{
	if(ev.type == EVENT_MOTION) {
		dispatch_event(&ev);
	}
}

static void dispatch_event(spnav_event *ev)
{
	struct client *c, *citer;
	static struct timeval prev_motion_time;

	if(ev->type == EVENT_MOTION) {
		struct timeval tv;
		gettimeofday(&tv, 0);

		ev->motion.period = msec_dif(tv, prev_motion_time);
		prev_motion_time = tv;
	}

	citer = first_client();
	while(citer) {
		c = citer;
		citer = next_client();

		send_event(ev, c);
	}
}

static void send_event(spnav_event *ev, struct client *c)
{
	switch(get_client_type(c)) {
#ifdef USE_X11
	case CLIENT_X11:
		send_xevent(ev, c);
		break;
#endif

	case CLIENT_UNIX:
		send_uevent(ev, c);
		break;

	default:
		break;
	}
}

static unsigned int msec_dif(struct timeval tv1, struct timeval tv2)
{
	unsigned int ms1, ms2;

	ms1 = tv1.tv_sec * 1000 + tv1.tv_usec / 1000;
	ms2 = tv2.tv_sec * 1000 + tv2.tv_usec / 1000;
	return ms1 - ms2;
}
