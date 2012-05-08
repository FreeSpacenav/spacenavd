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
#ifdef __linux__

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/input.h>
#include "dev.h"
#include "spnavd.h"
#include "event.h"
#include "hotplug.h"

#define IS_DEV_OPEN(dev) ((dev)->fd >= 0)

/* sometimes the rotation events are missing from linux/input.h */
#ifndef REL_RX
#define REL_RX	3
#endif
#ifndef REL_RY
#define REL_RY	4
#endif
#ifndef REL_RZ
#define REL_RZ	5
#endif

/* apparently some old versions of input.h do not define EV_SYN */
#ifndef EV_SYN
#define EV_SYN	0
#endif

static void close_evdev(struct device *dev);
static int read_evdev(struct device *dev, struct dev_input *inp);
static void set_led_evdev(struct device *dev, int state);


int open_dev_usb(struct device *dev, const char *path)
{
	/*unsigned char evtype_mask[(EV_MAX + 7) / 8];*/

	if((dev->fd = open(path, O_RDWR)) == -1) {
		if((dev->fd = open(path, O_RDONLY)) == -1) {
			perror("failed to open device");
			return -1;
		}
		fprintf(stderr, "opened device read-only, LEDs won't work\n");
	}

	if(ioctl(dev->fd, EVIOCGNAME(sizeof dev->name), dev->name) == -1) {
		perror("EVIOCGNAME ioctl failed\n");
		strcpy(dev->name, "unknown");
	}
	printf("device name: %s\n", dev->name);

	/*if(ioctl(dev->fd, EVIOCGBIT(0, sizeof(evtype_mask)), evtype_mask) == -1) {
		perror("EVIOCGBIT ioctl failed\n");
		close(dev->fd);
		return -1;
	}*/

	if(cfg.grab_device) {
		int grab = 1;
		/* try to grab the device */
		if(ioctl(dev->fd, EVIOCGRAB, &grab) == -1) {
			perror("failed to grab the spacenav device");
		}
	}

	/* set non-blocking */
	fcntl(dev->fd, F_SETFL, fcntl(dev->fd, F_GETFL) | O_NONBLOCK);

	if(cfg.led) {
		set_led_evdev(dev, 1);
	}

	/* fill the device function pointers */
	dev->close = close_evdev;
	dev->read = read_evdev;
	dev->set_led = set_led_evdev;

	return 0;
}

static void close_evdev(struct device *dev)
{
	if(IS_DEV_OPEN(dev)) {
		dev->set_led(dev, 0);
		close(dev->fd);
		dev->fd = -1;
	}
}

static int read_evdev(struct device *dev, struct dev_input *inp)
{
	struct input_event iev;	/* linux evdev event */
	int rdbytes;

	if(!IS_DEV_OPEN(dev))
		return -1;

	do {
		rdbytes = read(dev->fd, &iev, sizeof iev);
	} while(rdbytes == -1 && errno == EINTR);

	/* disconnect? */
	if(rdbytes == -1) {
		if(errno != EAGAIN) {
			perror("read error");
			close(dev->fd);
			dev->fd = -1;

			/* restart hotplug detection */
			init_hotplug();
		}
		return -1;
	}

	if(rdbytes > 0) {
		inp->tm = iev.time;

		switch(iev.type) {
		case EV_REL:
			inp->type = INP_MOTION;
			inp->idx = iev.code - REL_X;
			inp->val = iev.value;
			break;

		case EV_ABS:
			inp->type = INP_MOTION;
			inp->idx = iev.code - ABS_X;
			inp->val = iev.value;
			break;

		case EV_KEY:
			inp->type = INP_BUTTON;
			inp->idx = iev.code - BTN_0;
			inp->val = iev.value;
			break;

		case EV_SYN:
			inp->type = INP_FLUSH;
			break;

		default:
			return -1;
		}
	}

	return 0;

}

static void set_led_evdev(struct device *dev, int state)
{
	struct input_event ev;

	if(!IS_DEV_OPEN(dev))
		return;

	memset(&ev, 0, sizeof ev);
	ev.type = EV_LED;
	ev.code = LED_MISC;
	ev.value = state;

	if(write(dev->fd, &ev, sizeof ev) == -1) {
		fprintf(stderr, "failed to turn LED %s\n", state ? "on" : "off");
	}
}

#define PROC_DEV	"/proc/bus/input/devices"
const char *find_usb_device(void)
{
	static char path[PATH_MAX];
	int i, valid_vendor = 0, valid_str = 0;
	char buf[1024];
	FILE *fp;

	if(verbose) {
		printf("Device detection, parsing " PROC_DEV "\n");
	}

	if((fp = fopen(PROC_DEV, "r"))) {
		while(fgets(buf, sizeof buf, fp)) {
			switch(buf[0]) {
			case 'I':
				valid_vendor = strstr(buf, "Vendor=046d") != 0;
				break;

			case 'N':
				valid_str = strstr(buf, "3Dconnexion") != 0;
				break;

			case 'H':
				if(valid_str && valid_vendor) {
					char *ptr, *start;

					if(!(start = strchr(buf, '='))) {
						continue;
					}
					start++;

					if((ptr = strstr(start, "event"))) {
						start = ptr;
					}

					if((ptr = strchr(start, ' '))) {
						*ptr = 0;
					}
					if((ptr = strchr(start, '\n'))) {
						*ptr = 0;
					}

					snprintf(path, sizeof path, "/dev/input/%s", start);
					fclose(fp);
					return path;
				}
				break;

			case '\n':
				valid_vendor = valid_str = 0;
				break;

			default:
				break;
			}
		}
		fclose(fp);
	} else {
		if(verbose) {
			perror("failed to open " PROC_DEV);
		}
	}

	if(verbose) {
		fprintf(stderr, "trying alternative detection, querying /dev/input/eventX device names...\n");
	}

	/* if for some reason we can't open the /proc/bus/input/devices file, or we
	 * couldn't find our device there, we'll try opening all /dev/input/eventX
	 * devices, and see if anyone is named: 3Dconnexion whatever
	 */
	i = 0;
	for(;;) {
		int fd;

		snprintf(path, sizeof path, "/dev/input/event%d", ++i);

		if(verbose) {
			fprintf(stderr, "  trying \"%s\" ... ", path);
		}

		if((fd = open(path, O_RDONLY)) == -1) {
			if(errno != ENOENT) {
				fprintf(stderr, "failed to open %s: %s. this might hinder device detection\n",
						path, strerror(errno));
				continue;
			} else {
				fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
				break;
			}
		}

		if(ioctl(fd, EVIOCGNAME(sizeof buf), buf) == -1) {
			fprintf(stderr, "failed to get device name for device %s: %s. this might hinder device detection\n",
					path, strerror(errno));
			buf[0] = 0;
		}

		if(verbose) {
			fprintf(stderr, "%s\n", buf[0] ? buf : "unknown");
		}

		if(strstr(buf, "3Dconnexion")) {
			close(fd);
			return path;
		}
		close(fd);
	}

	return 0;
}


#endif	/* __linux__ */
