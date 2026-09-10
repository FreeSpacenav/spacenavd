/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2026 John Tsiombikas <nuclear@mutantstargoat.com>

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
#include <string.h>
#include "event.h"
#include "client.h"
#include "proto_unix.h"
#include "spnavd.h"
#include "kbemu.h"

#ifdef USE_X11
#include "proto_x11.h"
#endif

enum {
	MOT_X, MOT_Y, MOT_Z,
	MOT_RX, MOT_RY, MOT_RZ
};

enum {
	BTN_RELEASE = 0,
	BTN_PRESS = 1
};

struct dev_event {
	spnav_event event;
	struct timeval timeval;
	struct device *dev;
	int pending;
	struct dev_event *next;
	struct timeval hold_timeouts[MAX_BUTTONS];
};

static struct dev_event *add_dev_event(struct device *dev);
static struct dev_event *get_dev_event(struct device *dev);
static void handle_button_action(int act, int val);
static void dispatch_event(struct device *dev, spnav_event *ev);
static void send_event(spnav_event *ev, struct client *c);
static unsigned int msec_dif(struct timeval tv1, struct timeval tv2);

static struct dev_event *dev_ev_list = NULL;

static int disable_translation, disable_rotation, dom_axis_mode;
static int cur_axis_mag[6], cur_dom_axis;


static struct dev_event *add_dev_event(struct device *dev)
{
	struct dev_event *dev_ev, *iter;

	if((dev_ev = calloc(1, sizeof *dev_ev)) == NULL) {
		return NULL;
	}

	dev_ev->event.motion.data = (int*)&dev_ev->event.motion.x;
	gettimeofday(&dev_ev->timeval, 0);
	dev_ev->dev = dev;
	dev_ev->next = NULL;

	if(dev_ev_list == NULL)
		return dev_ev_list = dev_ev;

	iter = dev_ev_list;
	while(iter->next) {
		iter = iter->next;
	}
	iter->next = dev_ev;
	return dev_ev;
}

/* remove_dev_event takes a device pointer as argument so that upon removal of
 * a device the pending event (if any) can be removed.
 */
void remove_dev_event(struct device *dev)
{
	struct dev_event dummy;
	struct dev_event *iter;

	dummy.next = dev_ev_list;
	iter = &dummy;

	while(iter->next) {
		if(iter->next->dev == dev) {
			struct dev_event *ev = iter->next;
			iter->next = ev->next;

			if(verbose) {
				logmsg(LOG_INFO, "removing pending device event of: %s\n", dev->path);
			}
			free(ev);
		} else {
			iter = iter->next;
		}
	}
	dev_ev_list = dummy.next;
}

static struct dev_event *get_dev_event(struct device *dev)
{
	struct dev_event *iter = dev_ev_list;
	while(iter) {
		if(iter->dev == dev) {
			return iter;
		}
		iter = iter->next;
	}
	return NULL;
}

static INLINE int map_axis(int devaxis)
{
	const static int swaptab[] = {0, 2, 1, 3, 5, 4};

	int axis = cfg.map_axis[devaxis];
	if(axis < 0 || axis >= 6) {
		return -1;
	}

	if(cfg.swapyz) {
		return swaptab[axis];
	}
	return axis;
}

static void update_motion_period(struct dev_event *dev_ev)
{
	struct timeval now;
	gettimeofday(&now, 0);

	dev_ev->event.motion.period = msec_dif(now, dev_ev->timeval);
	dev_ev->timeval = now;
}

static void emit_button(struct device *dev, int idx, int val, int hold)
{
	unsigned int key, *keys;
	spnav_event ev;

	/* check to see if the button has been bound to an action */
	if(cfg.bnact[idx] > 0) {
		handle_button_action(cfg.bnact[idx], val);
		return;
	}

	/* check to see if we must emulate a keyboard event instead of a
	 * regular button event for this button
	 */
	if(cfg.kbmap_count[idx] == 1) {
		/* single key */
		key = cfg.kbmap[idx][0];
		kbemu_send_key(key, val);
		return;
	}

	if(cfg.kbmap_count[idx] > 1) {
		/* multi-key combo */
		keys = cfg.kbmap[idx];
		kbemu_send_combo(keys, cfg.kbmap_count[idx], val);
		return;
	}

	if(hold) {
		idx = cfg.map_hold[idx];
	} else {
		idx = cfg.map_button[idx];
	}

	ev.button.type = EVENT_BUTTON;
	ev.button.bnum = idx;
	ev.button.press = val;
	dispatch_event(dev, &ev);
}

/* process_input processes an device input event, and dispatches
 * spacenav events to the clients by calling dispatch_event.
 * relative inputs (INP_MOTION) are accumulated, and dispatched when
 * we get an INP_FLUSH event. Button events are dispatched immediately
 * and they implicitly flush any pending motion event.
 */
void process_input(struct device *dev, struct dev_input *inp)
{
	int sign, axis, abs_val, hold;
	struct dev_event *dev_ev;
	float sens_rot, sens_trans, axis_sens;
	spnav_event ev;
	struct timeval now, *timeout;

	dev_ev = get_dev_event(dev);
	if(verbose && dev_ev == NULL) {
		logmsg(LOG_INFO, "adding dev event for device: %s\n", dev->path);
	}
	if(dev_ev == NULL && (dev_ev = add_dev_event(dev)) == NULL) {
		logmsg(LOG_ERR, "failed to get dev_event\n");
		return;
	}

	switch(inp->type) {
	case INP_MOTION:
		ev.type = EVENT_RAWAXIS;
		ev.axis.idx = inp->idx;
		ev.axis.value = inp->val;
		broadcast_event(&ev);

		abs_val = abs(inp->val);

		if(abs_val < cfg.dead_threshold[inp->idx] ) {
			inp->val = 0;
		}
		if((axis = map_axis(inp->idx)) == -1) {
			break;
		}
		sign = cfg.invert[axis] ? -1 : 1;

		sens_rot = disable_rotation ? 0 : cfg.sens_rot[axis - 3];
		sens_trans = disable_translation ? 0 : cfg.sens_trans[axis];
		axis_sens = axis < 3 ? sens_trans : sens_rot;

		if(dom_axis_mode && axis < 6) {
			if(abs_val > cur_axis_mag[cur_dom_axis]) {
				cur_dom_axis = axis;
			} else {
				inp->val = 0;
			}
			cur_axis_mag[axis] = abs_val;
		}
		inp->val = (int)((float)inp->val * cfg.sensitivity * axis_sens);

		dev_ev->event.type = EVENT_MOTION;
		dev_ev->event.motion.data = (int*)&dev_ev->event.motion.x;
		dev_ev->event.motion.data[axis] = sign * inp->val;
		dev_ev->pending = 1;
		break;

	case INP_BUTTON:
		ev.type = EVENT_RAWBUTTON;
		ev.button.press = inp->val;
		ev.button.bnum = inp->idx;
		broadcast_event(&ev);

		if(dev_ev && dev_ev->pending) {
			update_motion_period(dev_ev);
			dispatch_event(dev_ev->dev, &dev_ev->event);
			dev_ev->pending = 0;
		}

		if(cfg.map_hold[inp->idx] == -1) {
			emit_button(dev_ev->dev, inp->idx, inp->val, 0);
			break;
		}

		gettimeofday(&now, NULL);
		timeout = dev_ev->hold_timeouts + inp->idx;

		if(inp->val) {
			*timeout = now;

			timeout->tv_sec += cfg.hold_timeout / 1000;
			timeout->tv_usec += cfg.hold_timeout % 1000 * 1000;

			timeout->tv_sec += timeout->tv_usec / 1000000;
			timeout->tv_usec = timeout->tv_usec % 1000000;

		} else if(timeout->tv_sec || timeout->tv_usec) {
			hold = 0;

			if(TIMERCMP(timeout, <=, &now)) {
				hold = 1;
			}

			emit_button(dev_ev->dev, inp->idx, 1, hold);
			emit_button(dev_ev->dev, inp->idx, 0, hold);

			timeout->tv_sec = 0;
			timeout->tv_usec = 0;
		}
		break;

	case INP_FLUSH:
		dev_ev = get_dev_event(dev);
		if(dev_ev && dev_ev->pending) {
			update_motion_period(dev_ev);
			dispatch_event(dev, &dev_ev->event);
			dev_ev->pending = 0;
		}
		break;

	default:
		break;
	}
}

static void handle_button_action(int act, int pressed)
{
	if(pressed) return;	/* perform all actions on release */

	switch(act) {
	case BNACT_SENS_INC:
		cfg.sensitivity *= 1.1f;
		broadcast_cfg_event(REQ_GCFG_SENS, *(int*)&cfg.sensitivity);
		break;
	case BNACT_SENS_DEC:
		cfg.sensitivity *= 0.9f;
		broadcast_cfg_event(REQ_GCFG_SENS, *(int*)&cfg.sensitivity);
		break;
	case BNACT_SENS_RESET:
		cfg.sensitivity = 1.0f;
		broadcast_cfg_event(REQ_GCFG_SENS, *(int*)&cfg.sensitivity);
		break;
	case BNACT_DISABLE_ROTATION:
		disable_rotation = !disable_rotation;
		if(disable_rotation) {
			disable_translation = 0;
		}
		break;
	case BNACT_DISABLE_TRANSLATION:
		disable_translation = !disable_translation;
		if(disable_translation) {
			disable_rotation = 0;
		}
		break;
	case BNACT_DOMINANT_AXIS:
		dom_axis_mode = !dom_axis_mode;
		break;
	}
}

int in_deadzone(struct device *dev)
{
	int i;
	struct dev_event *dev_ev;
	if((dev_ev = get_dev_event(dev)) == NULL)
		return -1;
	for(i=0; i<6; i++) {
		if(dev_ev->event.motion.data[i] != 0)
			return 0;
	}
	return 1;
}

void repeat_last_motion_event(struct device *dev)
{
	struct dev_event *dev_ev;

	if((dev_ev = get_dev_event(dev)) == NULL)
		return;

	update_motion_period(dev_ev);
	dispatch_event(dev, &dev_ev->event);
}

int next_button_timeout(struct device *head, struct timeval *timeout)
{
	unsigned int i;
	struct dev_event *dev_ev;
	struct timeval now, diff, *holdtm, *min = NULL;

	while(head) {
		if(!is_device_valid(head)) goto next;

		if(!(dev_ev = get_dev_event(head))) {
			goto next;
		}

		for(i=0; i<MAX_BUTTONS; i++) {
			holdtm = dev_ev->hold_timeouts + i;

			if(!holdtm->tv_sec && !holdtm->tv_usec) {
				continue;
			}

			if(!min || TIMERCMP(holdtm, <, min)) {
				min = holdtm;
			}
		}

next:	head = head->next;
	}

	if(!min) return 1;

	gettimeofday(&now, 0);
	diff.tv_sec = min->tv_sec - now.tv_sec;
	diff.tv_usec = min->tv_usec - now.tv_usec;
	if(diff.tv_usec < 0) {
		diff.tv_sec--;
		diff.tv_usec += 1000000;
	}
	if(diff.tv_sec < 0) {
		diff.tv_sec = 0;
		diff.tv_usec = 0;
	}

	*timeout = diff;
	return 0;
}

void emit_button_timeouts(struct device *head)
{
	struct dev_event *dev_ev;
	struct timeval now, *timeout;
	unsigned int i;

	gettimeofday(&now, 0);
	while(head) {
		if(!(dev_ev = get_dev_event(head))) {
			goto next;
		}

		for(i=0; i<MAX_BUTTONS; i++) {
			timeout = dev_ev->hold_timeouts + i;

			if(!timeout->tv_sec && !timeout->tv_usec) {
				continue;
			}

			if(TIMERCMP(timeout, <=, &now)) {
				emit_button(dev_ev->dev, i, 1, 1);
				emit_button(dev_ev->dev, i, 0, 1);

				timeout->tv_sec = 0;
				timeout->tv_usec = 0;
			}
		}

next:	head = head->next;
	}
}

static void dispatch_event(struct device *dev, spnav_event *ev)
{
	struct client *c, *client_iter;
	struct device *client_dev;

	client_iter = first_client();
	while(client_iter) {
		c = client_iter;
		client_iter = next_client();

		/* if the client has selected a single device to get input from, then
		 * don't send the event if it originates from a different device
		 */
		client_dev = get_client_device(c);
		if(!client_dev || client_dev == dev) {
			send_event(ev, c);
		}
	}
}

void broadcast_event(spnav_event *ev)
{
	struct client *c;

	c = first_client();
	while(c) {
		/* event masks will be checked at the protocol level (send_uevent) */
		send_event(ev, c);
		c = c->next;
	}
}

void broadcast_cfg_event(int cfg, int val)
{
	spnav_event ev = {0};

	ev.type = EVENT_CFG;
	ev.cfg.cfg = cfg;
	ev.cfg.data[0] = val;
	broadcast_event(&ev);
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
