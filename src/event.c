/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2023 John Tsiombikas <nuclear@member.fsf.org>

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

#ifdef USE_X11
#include "proto_x11.h"
#include "kbemu.h"
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
};

static struct dev_input inp_dom = { -1, {0}, -1, 0 };
static int dom_axis_thres = 2;

static struct dev_event *add_dev_event(struct device *dev);
static struct dev_event *device_event_in_use(struct device *dev);
static void handle_button_action(int act, int val);
static void dispatch_event(struct device *dev, struct dev_event *dev_ev);
static void send_event(struct device *dev, spnav_event *ev, struct client *c);
static unsigned int msec_dif(struct timeval tv1, struct timeval tv2);

static struct dev_event *dev_ev_list = NULL;

static int disable_translation, disable_rotation, dom_axis_mode;


static struct dev_event *add_dev_event(struct device *dev)
{
	struct dev_event *dev_ev, *iter;
	int i;

	if((dev_ev = malloc(sizeof *dev_ev)) == NULL) {
		return NULL;
	}

	dev_ev->event.motion.data = (int*)&dev_ev->event.motion.x;
	for(i=0; i<6; i++)
		dev_ev->event.motion.data[i] = 0;
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

static struct dev_event *device_event_in_use(struct device *dev)
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

static inline int map_axis(int devaxis)
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

/* process_input processes an device input event, and dispatches
 * spacenav events to the clients by calling dispatch_event.
 * relative inputs (INP_MOTION) are accumulated, and dispatched when
 * we get an INP_FLUSH event. Button events are dispatched immediately
 * and they implicitly flush any pending motion event.
 */
void process_input(struct device *dev, struct dev_input *inp)
{
	int sign, axis;
	struct dev_event *dev_ev;
	float sens_rot, sens_trans, axis_sens;
	spnav_event ev;

	switch(inp->type) {
	case INP_MOTION:
		ev.type = EVENT_RAWAXIS;
		ev.axis.idx = inp->idx;
		ev.axis.value = inp->val;
		broadcast_event(dev, &ev);

		if(abs(inp->val) < cfg.dead_threshold[inp->idx] ) {
			inp->val = 0;
		}
		if((axis = map_axis(inp->idx)) == -1) {
			break;
		}
		sign = cfg.invert[axis] ? -1 : 1;

		sens_rot = disable_rotation ? 0 : cfg.sens_rot[axis - 3];
		sens_trans = disable_translation ? 0 : cfg.sens_trans[axis];
		axis_sens = axis < 3 ? sens_trans : sens_rot;

		if(dom_axis_mode) {
			if(inp_dom.idx != -1) {
				/* if more than 100ms have passed ... */
				if(inp->tm.tv_sec > inp_dom.tm.tv_sec || inp->tm.tv_usec - inp_dom.tm.tv_usec >= 100000) {
					inp_dom.idx = -1;
					memset(&inp_dom.tm, 0, sizeof inp_dom.tm);
					inp_dom.type = INP_FLUSH;
					inp_dom.val = 0;
				}
			}
			if((inp_dom.idx == -1 && (inp->val <= dom_axis_thres || inp->val >= dom_axis_thres))
					|| inp_dom.idx == axis) {
				inp_dom.idx = axis;
				inp_dom.tm = inp->tm;
				inp_dom.type = inp->type;
				inp_dom.val = inp->val;
			} else {
				axis_sens = 0;
			}
		}
		inp->val = (int)((float)inp->val * cfg.sensitivity * axis_sens);

		dev_ev = device_event_in_use(dev);
		if(verbose && dev_ev == NULL)
			logmsg(LOG_INFO, "adding dev event for device: %s\n", dev->path);
		if(dev_ev == NULL && (dev_ev = add_dev_event(dev)) == NULL) {
			logmsg(LOG_ERR, "failed to get dev_event\n");
			break;
		}
		dev_ev->event.type = EVENT_MOTION;
		dev_ev->event.motion.data = (int*)&dev_ev->event.motion.x;
		dev_ev->event.motion.data[axis] = sign * inp->val;
		dev_ev->pending = 1;
		break;

	case INP_BUTTON:
		ev.type = EVENT_RAWBUTTON;
		ev.button.press = inp->val;
		ev.button.bnum = inp->idx;
		broadcast_event(dev, &ev);

		/* check to see if the button has been bound to an action */
		if(cfg.bnact[inp->idx] > 0) {
			handle_button_action(cfg.bnact[inp->idx], inp->val);
			break;
		}

#ifdef USE_X11
		/* check to see if we must emulate a keyboard event instead of a
		 * retular button event for this button
		 */
		if(cfg.kbmap_str[inp->idx]) {
			if(!cfg.kbmap[inp->idx]) {
				cfg.kbmap[inp->idx] = kbemu_keysym(cfg.kbmap_str[inp->idx]);
				if(verbose) {
					logmsg(LOG_DEBUG, "mapping ``%s'' to keysym %d\n", cfg.kbmap_str[inp->idx], (int)cfg.kbmap[inp->idx]);
				}
			}
			send_kbevent(cfg.kbmap[inp->idx], inp->val);
			break;
		}
#endif
		dev_ev = device_event_in_use(dev);
		if(dev_ev && dev_ev->pending) {
			dispatch_event(dev, dev_ev);
			dev_ev->pending = 0;
		}
		inp->idx = cfg.map_button[inp->idx];

		/* button events are not queued */
		{
			struct dev_event dev_button_event;
			dev_button_event.dev = dev;
			dev_button_event.event.type = EVENT_BUTTON;
			dev_button_event.event.button.press = inp->val;
			dev_button_event.event.button.bnum = inp->idx;
			dispatch_event(dev, &dev_button_event);
		}

		/* to have them replace motion events in the queue uncomment next section */
		/* dev_ev = add_dev_event(dev);
		 * dev_ev->event.type = EVENT_BUTTON;
		 * dev_ev->event.button.press = inp->val;
		 * dev_ev->event.button.bnum = inp->idx;
		 * dispatch_event(dev, dev_ev);
		 */
		break;

	case INP_FLUSH:
		dev_ev = device_event_in_use(dev);
		if(dev_ev && dev_ev->pending) {
			dispatch_event(dev, dev_ev);
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
	if((dev_ev = device_event_in_use(dev)) == NULL)
		return -1;
	for(i=0; i<6; i++) {
		if(dev_ev->event.motion.data[i] != 0)
			return 0;
	}
	return 1;
}

void repeat_last_event(struct device *dev)
{
	struct dev_event *dev_ev;
	if((dev_ev = device_event_in_use(dev)) == NULL)
		return;
	dispatch_event(dev, dev_ev);
}

static void dispatch_event(struct device *dev, struct dev_event *dev_ev)
{
	struct client *c, *client_iter;
	struct device *client_dev;

	if(dev_ev->event.type == EVENT_MOTION) {
		struct timeval tv;
		gettimeofday(&tv, 0);

		dev_ev->event.motion.period = msec_dif(tv, dev_ev->timeval);
		dev_ev->timeval = tv;
	}

	client_iter = first_client();
	while(client_iter) {
		c = client_iter;
		client_iter = next_client();

		/* if the client has selected a single device to get input from, then
		 * don't send the event if it originates from a different device
		 * as of January 2024, there is no protocol mechanism for a client to
		 * select a single device.
		 * However, clients may select to receive events from all devices.
		 */
		client_dev = get_client_device(c);
		if(!client_dev || client_dev == dev_ev->dev || is_client_multidev(c)) {
			send_event(dev, &dev_ev->event, c);
		}
	}
}

void broadcast_event(struct device *maybe_dev, spnav_event *ev)
{
	struct client *c;

	c = first_client();
	while(c) {
		/* event masks will be checked at the protocol level (send_uevent) */
		send_event(maybe_dev, ev, c);
		c = c->next;
	}
}

void broadcast_cfg_event(int cfg, int val)
{
	spnav_event ev = {0};

	ev.type = EVENT_CFG;
	ev.cfg.cfg = cfg;
	ev.cfg.data[0] = val;
	broadcast_event(NULL, &ev);
}

static void send_event(struct device *dev, spnav_event *ev, struct client *c)
{
	switch(get_client_type(c)) {
#ifdef USE_X11
	case CLIENT_X11:
		send_xevent(ev, c);
		break;
#endif

	case CLIENT_UNIX:
		send_uevent(dev, ev, c);
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
