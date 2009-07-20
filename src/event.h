#ifndef EVENT_H_
#define EVENT_H_

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
