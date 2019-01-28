/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2013 John Tsiombikas <nuclear@member.fsf.org>

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

struct dev_event {
	spnav_event event;
	struct timeval timeval;
	struct device *dev;
	int pending;
	struct dev_event *next;
};

static struct dev_event *add_dev_event(struct device *dev);
static struct dev_event *device_event_in_use(struct device *dev);
static void dispatch_event(struct dev_event *dev);
static void send_event(spnav_event *ev, struct client *c);
static unsigned int msec_dif(struct timeval tv1, struct timeval tv2);

static struct dev_event *dev_ev_list = NULL;

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

/* process_input processes an device input event, and dispatches
 * spacenav events to the clients by calling dispatch_event.
 * relative inputs (INP_MOTION) are accumulated, and dispatched when
 * we get an INP_FLUSH event. Button events are dispatched immediately
 * and they implicitly flush any pending motion event.
 */
void process_input(struct device *dev, struct dev_input *inp)
{
	int sign;
	struct dev_event *dev_ev;

	switch(inp->type) {
	case INP_MOTION:
		inp->idx = cfg.map_axis[inp->idx];

		if(abs(inp->val) < cfg.dead_threshold[inp->idx] ) {
			inp->val = 0;
		}
		sign = cfg.invert[inp->idx] ? -1 : 1;

		inp->val = (int)((float)inp->val * cfg.sensitivity * (inp->idx < 3 ? cfg.sens_trans[inp->idx] : cfg.sens_rot[inp->idx - 3]));

		dev_ev = device_event_in_use(dev);
		if(verbose && dev_ev == NULL)
			logmsg(LOG_INFO, "adding dev event for device: %s\n", dev->path);
		if(dev_ev == NULL && (dev_ev = add_dev_event(dev)) == NULL) {
			logmsg(LOG_ERR, "failed to get dev_event\n");
			break;
		}
		dev_ev->event.type = EVENT_MOTION;
		dev_ev->event.motion.data = (int*)&dev_ev->event.motion.x;
		dev_ev->event.motion.data[inp->idx] = sign * inp->val;
		dev_ev->pending = 1;
		break;

	case INP_BUTTON:
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
			dispatch_event(dev_ev);
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
			dispatch_event(&dev_button_event);
		}

		/* to have them replace motion events in the queue uncomment next section */
		/* dev_ev = add_dev_event(dev);
		 * dev_ev->event.type = EVENT_BUTTON;
		 * dev_ev->event.button.press = inp->val;
		 * dev_ev->event.button.bnum = inp->idx;
		 * dispatch_event(dev_ev);
		 */
		break;

	case INP_FLUSH:
		dev_ev = device_event_in_use(dev);
		if(dev_ev && dev_ev->pending) {
			dispatch_event(dev_ev);
			dev_ev->pending = 0;
		}
		break;

	default:
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
	dispatch_event(dev_ev);
}

static void dispatch_event(struct dev_event *dev_ev)
{
	struct client *c, *client_iter;
	int dev_idx;

	if(dev_ev->event.type == EVENT_MOTION) {
		struct timeval tv;
		gettimeofday(&tv, 0);

		dev_ev->event.motion.period = msec_dif(tv, dev_ev->timeval);
		dev_ev->timeval = tv;
	}

	dev_idx = get_device_index(dev_ev->dev);
	client_iter = first_client();
	while(client_iter) {
		c = client_iter;
		client_iter = next_client();
		if(get_client_device_index(c) <= dev_idx) /* use <= until API changes, else == */
			send_event(&dev_ev->event, c);
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
