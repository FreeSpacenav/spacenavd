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
#ifdef __linux__

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include "event.h"
#include "dev.h"
#include "dev_js.h"

#define IS_DEV_OPEN(dev) ((dev)->fd >= 0)

static void close_jsdev(struct device *dev);
static int read_jsdev(struct device *dev, struct dev_input *inp);

int open_dev_js(struct device *dev)
{
	int naxes = 6;
	int nbuttons = 0;

	if((dev->fd = open(dev->path, O_RDONLY)) == -1) {
		fprintf(stderr, "failed to open joystick device: %s: %s\n", dev->path, strerror(errno));
		return -1;
	}

	if(ioctl(dev->fd, JSIOCGNAME(sizeof dev->name), dev->name) == -1) {
		perror("JSIOCGNAME failed");
		strcpy(dev->name, "unknown joystick");
	}
	printf("device (joystick) name: %s\n", dev->name);

	if(ioctl(dev->fd, JSIOCGAXES, &naxes) == -1) {
		perror("failed to get number of axes");
	}
	if(ioctl(dev->fd, JSIOCGBUTTONS, &nbuttons) == -1) {
		perror("failed to get number of buttons");
	}

	fcntl(dev->fd, F_SETFL, fcntl(dev->fd, F_GETFL) | O_NONBLOCK);

	dev->close = close_jsdev;
	dev->read = read_jsdev;
	dev->set_led = 0;
	return 0;
}

static void close_jsdev(struct device *dev)
{
	if(IS_DEV_OPEN(dev)) {
		close(dev->fd);
		dev->fd = -1;
	}
}

static int read_jsdev(struct device *dev, struct dev_input *inp)
{
	struct js_event jev;
	int rdbytes;

	if(!IS_DEV_OPEN(dev)) {
		return -1;
	}

	do {
		rdbytes = read(dev->fd, &jev, sizeof jev);
	} while(rdbytes == -1 && errno == EINTR);

	/* disconnect? */
	if(rdbytes == -1) {
		if(errno != EAGAIN) {
			perror("js read error");
			remove_device(dev);
		}
		return -1;
	}

	if(rdbytes > 0) {
		inp->tm.tv_sec = jev.time / 1000;
		inp->tm.tv_usec = (jev.time % 1000) * 1000;

		if(jev.type & JS_EVENT_AXIS) {
			inp->type = INP_MOTION;
			inp->idx = jev.number;
			inp->val = (float)jev.value / (float)SHRT_MAX;
		} else if(jev.type & JS_EVENT_BUTTON) {
			inp->type = INP_BUTTON;
			inp->idx = jev.number;
			inp->val = jev.value;
		} else {
			fprintf(stderr, "js input: unknown event type (%x)\n", (unsigned int)jev.type);
			return -1;
		}
	}
	return 0;
}

#endif	/* __linux__ */
