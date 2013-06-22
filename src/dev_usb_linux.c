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
#include <stdlib.h>
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


int open_dev_usb(struct device *dev)
{
	/*unsigned char evtype_mask[(EV_MAX + 7) / 8];*/

	if((dev->fd = open(dev->path, O_RDWR)) == -1) {
		if((dev->fd = open(dev->path, O_RDONLY)) == -1) {
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
			remove_device(dev);
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
void find_usb_devices(char **path, int str_n, int char_n)
{
	int path_idx = 0;
	int i, valid_vendor = 0, valid_str = 0;
	int skip_section = 0, buf_used, buf_len;
	char buf[1024];
	char *buf_pos, *section_start, *next_section = 0, *cur_line, *next_line;
	FILE *fp;

	if(verbose) {
		printf("Device detection, parsing " PROC_DEV "\n");
	}

	for(i=0; i<str_n; i++) {
		path[i][0] = 0;
	}

	buf_pos = buf;
	buf_len = sizeof(buf);
	if((fp = fopen(PROC_DEV, "r"))) {
		while(fread(buf_pos, 1, buf_len, fp) > 0) {
			section_start = buf;

			for(;;) {
				next_section = strstr(section_start, "\n\n");
				if(next_section == NULL) {
					/* move last (partial) section to start of buf */
					buf_used = (buf + sizeof(buf)) - section_start;
					memmove(buf, section_start, buf_used);
					/* point to end of last section and calc remaining space in buf */
					buf_pos = buf + buf_used;
					buf_len = sizeof(buf) - buf_used;
					/* break to read from file again */
					break;
				}
				/* set second newline to teminating null */
				next_section[1] = 0;
				/* point to start of next section */
				next_section += 2;

				valid_vendor = 0;
				valid_str = 0;
				cur_line = section_start;
				while (*cur_line) {
					next_line = strchr(cur_line, '\n');
					*next_line = 0;
					next_line++;
					switch (*cur_line) {
						case 'I':
							valid_vendor = strstr(cur_line, "Vendor=046d") != 0;
							break;

						case 'N':
							valid_str = strstr(cur_line, "3Dconnexion") != 0;
							break;

						case 'H':
							if(valid_vendor && valid_str) {
								char *ptr, *start;

								if(!(start = strchr(cur_line, '='))) {
									skip_section = 1;
									break;
								}
								start++;

								if((ptr = strstr(start, "event"))) {
									start = ptr;
								}

								if((ptr = strchr(start, ' '))) {
									*ptr = 0;
								}

								snprintf(path[path_idx], char_n, "/dev/input/%s", start);
								path_idx++;
								if(path_idx == str_n) {
									return;
								}
							} else {
								skip_section = 1;
								break;
							}
					}
					if(skip_section) {
						skip_section = 0;
						break;
					}
					cur_line = next_line;
				}
				section_start = next_section;
			}
		}
		fclose(fp);
	} else {
		if(verbose) {
			perror("failed to open " PROC_DEV);
		}
	}

	if(path[0][0] != 0) {
		return;
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

		snprintf(path[path_idx], char_n, "/dev/input/event%d", ++i);

		if(verbose) {
			fprintf(stderr, "  trying \"%s\" ... ", path[path_idx]);
		}

		if((fd = open(path[path_idx], O_RDONLY)) == -1) {
			if(errno != ENOENT) {
				fprintf(stderr, "failed to open %s: %s. this might hinder device detection\n",
						path[path_idx], strerror(errno));
				continue;
			} else {
				fprintf(stderr, "failed to open %s: %s\n", path[path_idx], strerror(errno));
				path[path_idx][0] = 0;
				break;
			}
		}

		if(ioctl(fd, EVIOCGNAME(sizeof buf), buf) == -1) {
			fprintf(stderr, "failed to get device name for device %s: %s. this might hinder device detection\n",
					path[path_idx], strerror(errno));
			buf[0] = 0;
		}

		if(verbose) {
			fprintf(stderr, "%s\n", buf[0] ? buf : "unknown");
		}

		if(strstr(buf, "3Dconnexion")) {
			close(fd);
			path_idx++;
			if(path_idx == str_n) {
				return;
			}
		}
		close(fd);
	}

	return;
}

#endif	/* __linux__ */
