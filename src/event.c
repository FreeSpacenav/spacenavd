#include <stdlib.h>
#include "event.h"
#include "client.h"
#include "proto_x11.h"
#include "proto_unix.h"
#include "spnavd.h"

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
		if(abs(inp->val) < cfg.dead_threshold) {
			break;
		}

		inp->idx = cfg.map_axis[inp->idx];
		sign = cfg.invert[inp->idx] ? -1 : 1;

		if(cfg.sensitivity != 1.0) {
			inp->val = (int)((float)inp->val * cfg.sensitivity);
		}

		ev.type = EVENT_MOTION;
		ev.motion.data = (int*)&ev.motion.x;
		ev.motion.data[inp->idx] = sign * inp->val;
		ev_pending = 1;
		break;

	case INP_BUTTON:
		if(ev_pending) {
			dispatch_event(&ev);
			ev_pending = 0;
		}
		inp->idx = cfg.map_button[inp->idx];
		
		ev.type = EVENT_BUTTON;
		ev.button.press = inp->val;
		ev.button.bnum = inp->idx;
		dispatch_event(&ev);
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
	unsigned int ds, du;

	ds = tv2.tv_sec - tv1.tv_sec;
	du = tv2.tv_usec - tv1.tv_usec;
	return ds * 1000 + du / 1000;
}
