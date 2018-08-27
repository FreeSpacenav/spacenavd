/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2018 John Tsiombikas <nuclear@member.fsf.org>

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
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/input.h>
#include "dev.h"
#include "dev_usb.h"
#include "spnavd.h"
#include "event.h"
#include "hotplug.h"
#include "client.h"

#define DEF_MINVAL	(-500)
#define DEF_MAXVAL	500
#define DEF_RANGE	(DEF_MAXVAL - DEF_MINVAL)

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
	int i, axes_rel = 0, axes_abs = 0;
	struct input_absinfo absinfo;
	unsigned char evtype_mask[((EV_MAX | KEY_MAX) + 7) / 8];

	if((dev->fd = open(dev->path, O_RDWR)) == -1) {
		if((dev->fd = open(dev->path, O_RDONLY)) == -1) {
			perror("failed to open device");
			return -1;
		}
		fprintf(stderr, "opened device read-only, LEDs won't work\n");
	}

	if(ioctl(dev->fd, EVIOCGNAME(sizeof dev->name), dev->name) == -1) {
		perror("EVIOCGNAME ioctl failed");
		strcpy(dev->name, "unknown");
	}
	printf("device name: %s\n", dev->name);

	/* get number of axes */
	if(ioctl(dev->fd, EVIOCGBIT(EV_ABS, sizeof evtype_mask), evtype_mask) != -1) {
		for(i=0; i<ABS_CNT; i++) {
			int idx = i / 8;
			int bit = i % 8;

			if(evtype_mask[idx] & (1 << bit)) {
				axes_abs++;
			} else {
				break;
			}
		}
	} else {
		perror("EVIOCGBIT(EV_ABS) ioctl failed");
	}
	if(ioctl(dev->fd, EVIOCGBIT(EV_REL, sizeof evtype_mask), evtype_mask) != -1) {
		for(i=0; i<ABS_CNT; i++) {
			int idx = i / 8;
			int bit = i % 8;

			if(evtype_mask[idx] & (1 << bit)) {
				axes_rel++;
			}
		}
	}
	dev->num_axes = axes_rel + axes_abs;
	if(!dev->num_axes) {
		fprintf(stderr, "failed to retrieve number of axes. assuming 6\n");
		dev->num_axes = 6;
	} else {
		if(verbose) {
			printf("  Number of axes: %d (%da %dr)\n", dev->num_axes, axes_abs, axes_rel);
		}
	}

	/* get number of buttons */
	dev->num_buttons = 0;
	if(ioctl(dev->fd, EVIOCGBIT(EV_KEY, sizeof evtype_mask), evtype_mask) != -1) {
		for(i=0; i<KEY_CNT; i++) {
			int idx = i / 8;
			int bit = i % 8;

			if(evtype_mask[idx] & (1 << bit)) {
				dev->num_buttons++;
			}
		}
	} else {
		perror("EVIOCGBIT(EV_KEY) ioctl failed");
	}
	if(!dev->num_buttons) {
		fprintf(stderr, "failed to retrieve number of buttons, will default to 2\n");
		dev->num_buttons = 2;
	} else {
		if(verbose) {
			printf("  Number of buttons: %d\n", dev->num_buttons);
		}
	}

	dev->minval = malloc(dev->num_axes * sizeof *dev->minval);
	dev->maxval = malloc(dev->num_axes * sizeof *dev->maxval);
	dev->fuzz = malloc(dev->num_axes * sizeof *dev->fuzz);

	if(!dev->minval || !dev->maxval || !dev->fuzz) {
		perror("failed to allocate memory");
		return -1;
	}

	/* if the device is an absolute device, find the minimum and maximum axis values */
	for(i=0; i<dev->num_axes; i++) {
		dev->minval[i] = DEF_MINVAL;
		dev->maxval[i] = DEF_MAXVAL;
		dev->fuzz[i] = 0;

		if(ioctl(dev->fd, EVIOCGABS(i), &absinfo) == 0) {
			dev->minval[i] = absinfo.minimum;
			dev->maxval[i] = absinfo.maximum;
			dev->fuzz[i] = absinfo.fuzz;

			if(verbose) {
				printf("  Axis %d value range: %d - %d (fuzz: %d)\n", i, dev->minval[i], dev->maxval[i], dev->fuzz[i]);
			}
		}
	}

	if(cfg.grab_device) {
		int grab = 1;
		/* try to grab the device */
		if(ioctl(dev->fd, EVIOCGRAB, &grab) == -1) {
			perror("failed to grab the device");
		}
	}

	/* set non-blocking */
	fcntl(dev->fd, F_SETFL, fcntl(dev->fd, F_GETFL) | O_NONBLOCK);

	if(cfg.led == 1 || (cfg.led == 2 && first_client())) {
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
		free(dev->minval);
		free(dev->maxval);
		free(dev->fuzz);
	}
}

static INLINE int map_range(struct device *dev, int axidx, int val)
{
	int range = dev->maxval[axidx] - dev->minval[axidx];
	if(range <= 0) {
		return val;
	}

	return (val - dev->minval[axidx]) * DEF_RANGE / range + DEF_MINVAL;
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
			/*printf("[%s] EV_REL(%d): %d\n", dev->name, inp->idx, iev.value);*/
			break;

		case EV_ABS:
			inp->type = INP_MOTION;
			inp->idx = iev.code - ABS_X;
			inp->val = map_range(dev, inp->idx, iev.value);
			/*printf("[%s] EV_ABS(%d): %d (orig: %d)\n", dev->name, inp->idx, inp->val, iev.value);*/
			break;

		case EV_KEY:
			inp->type = INP_BUTTON;
			inp->idx = iev.code - BTN_0;
			inp->val = iev.value;
			break;

		case EV_SYN:
			inp->type = INP_FLUSH;
			/*printf("[%s] EV_SYN\n", dev->name);*/
			break;

		default:
			if(verbose) {
				printf("unhandled event: %d\n", iev.type);
			}
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
struct usb_device_info *find_usb_devices(int (*match)(const struct usb_device_info*))
{
	struct usb_device_info *devlist = 0, devinfo;
	int i, buf_used, buf_len, bytes_read;
	char buf[1024];
	char *buf_pos, *section_start, *next_section = 0, *cur_line, *next_line;
	FILE *fp;
	DIR *dir;
	struct dirent *dent;

	if(verbose) {
		printf("Device detection, parsing " PROC_DEV "\n");
	}

	devlist = 0;

	buf_pos = buf;
	buf_len = sizeof(buf) - 1;
	if(!(fp = fopen(PROC_DEV, "r"))) {
		if(verbose) {
			perror("failed to open " PROC_DEV);
		}
		goto alt_detect;
	}

	while((bytes_read = fread(buf_pos, 1, buf_len, fp)) >= 0) {
		buf_pos[bytes_read] = '\0';
		section_start = buf;

		for(;;) {
			char *keyptr;

			next_section = strstr(section_start, "\n\n");
			if(next_section == NULL) {
				/* move last (partial) section to start of buf */
				/* sizeof(buf) - 1 because the last one is '\0' */
				buf_used = strlen(section_start);
				memmove(buf, section_start, buf_used);
				buf[buf_used] = '\0';
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

			memset(&devinfo, 0, sizeof devinfo);

			cur_line = section_start;
			while (*cur_line) {
				next_line = strchr(cur_line, '\n');
				*next_line = 0;
				next_line++;
				switch (*cur_line) {
				case 'I':
					keyptr = strstr(cur_line, "Vendor=");
					if(keyptr) {
						char *endp, *valptr = keyptr + strlen("Vendor=");
						devinfo.vendorid = strtol(valptr, &endp, 16);
					}
					keyptr = strstr(cur_line, "Product=");
					if(keyptr) {
						char *endp, *valptr = keyptr + strlen("Product=");
						devinfo.productid = strtol(valptr, &endp, 16);
					}
					break;

				case 'N':
					keyptr = strstr(cur_line, "Name=\"");
					if(keyptr) {
						char *valptr = keyptr + strlen("Name=\"");
						char *endp = strrchr(cur_line, '"');
						if(endp) {
							*endp = 0;
						}
						if(!(devinfo.name = strdup(valptr))) {
							fprintf(stderr, "failed to allocate the device name buffer for: %s: %s\n", valptr, strerror(errno));
						}
					}
					break;

				case 'H':
					keyptr = strstr(cur_line, "Handlers=");
					if(keyptr) {
						char *devfile = 0, *valptr = keyptr + strlen("Handlers=");
						static const char *prefix = "/dev/input/";

						int idx = 0;
						while((devfile = strtok(devfile ? 0 : valptr, " \t\v\n\r"))) {
							if(strstr(devfile, "event") != devfile) {
								/* ignore everything which isn't an event interface device */
								continue;
							}

							if(!(devinfo.devfiles[idx] = malloc(strlen(devfile) + strlen(prefix) + 1))) {
								perror("failed to allocate device filename buffer");
								continue;
							}
							sprintf(devinfo.devfiles[idx++], "%s%s", prefix, devfile);
						}
						devinfo.num_devfiles = idx;
					}
					break;

				}
				cur_line = next_line;
			}

			/* check with the user-supplied matching callback to see if we should include
			 * this device in the returned list or not...
			 */
			if(devinfo.num_devfiles > 0 && (!match || match(&devinfo))) {
				/* add it to the list */
				struct usb_device_info *node = malloc(sizeof *node);
				if(node) {
					if(verbose) {
						printf("found usb device [%x:%x]: \"%s\" (%s) \n", devinfo.vendorid, devinfo.productid,
								devinfo.name ? devinfo.name : "unknown", devinfo.devfiles[0]);
					}

					*node = devinfo;
					memset(&devinfo, 0, sizeof devinfo);

					node->next = devlist;
					devlist = node;
				} else {
					perror("failed to allocate usb device info node");
				}
			} else {
				/* cleanup devinfo before moving to the next line */
				for(i=0; i<devinfo.num_devfiles; i++) {
					free(devinfo.devfiles[i]);
				}
				free(devinfo.name);
				memset(&devinfo, 0, sizeof devinfo);
			}

			section_start = next_section;
		}
		if(bytes_read == 0)
			break;
	}
	fclose(fp);

	if(devlist) {
		return devlist;
	}
	/* otherwise try the alternative detection in case it finds something... */

alt_detect:
	if(verbose) {
		fprintf(stderr, "trying alternative detection, querying /dev/input/ devices...\n");
	}

	/* if for some reason we can't open the /proc/bus/input/devices file, or we
	 * couldn't find our device there, we'll try opening all /dev/input/
	 * devices, and see if anyone matches our predicate
	 */
	if(!(dir = opendir("/dev/input"))) {
		perror("failed to open /dev/input/ directory");
		return 0;
	}

	while((dent = readdir(dir))) {
		int fd;
		struct stat st;
		struct input_id id;

		memset(&devinfo, 0, sizeof devinfo);

		if(!(devinfo.devfiles[0] = malloc(strlen(dent->d_name) + strlen("/dev/input/") + 1))) {
			perror("failed to allocate device file name");
			continue;
		}
		sprintf(devinfo.devfiles[0], "/dev/input/%s", dent->d_name);
		devinfo.num_devfiles = 1;

		if(verbose) {
			fprintf(stderr, "  trying \"%s\" ... ", devinfo.devfiles[0]);
		}

		if(stat(devinfo.devfiles[0], &st) == -1 || !S_ISCHR(st.st_mode)) {
			free(devinfo.devfiles[0]);
			continue;
		}

		if((fd = open(devinfo.devfiles[0], O_RDONLY)) == -1) {
			fprintf(stderr, "failed to open %s: %s\n", devinfo.devfiles[0], strerror(errno));
			free(devinfo.devfiles[0]);
			continue;
		}

		if(ioctl(fd, EVIOCGID, &id) != -1) {
			devinfo.vendorid = id.vendor;
			devinfo.productid = id.product;
		}

		if(ioctl(fd, EVIOCGNAME(sizeof buf), buf) != -1) {
			if(!(devinfo.name = strdup(buf))) {
				perror("failed to allocate device name buffer");
				close(fd);
				free(devinfo.devfiles[0]);
				continue;
			}
		}

		if(!match || match(&devinfo)) {
			struct usb_device_info *node = malloc(sizeof *node);
			if(node) {
				if(verbose) {
					printf("found usb device [%x:%x]: \"%s\" (%s) \n", devinfo.vendorid, devinfo.productid,
							devinfo.name ? devinfo.name : "unknown", devinfo.devfiles[0]);
				}

				*node = devinfo;
				node->next = devlist;
				devlist = node;
			} else {
				free(devinfo.name);
				free(devinfo.devfiles[0]);
				perror("failed to allocate usb device info");
			}
		} else {
			free(devinfo.name);
			free(devinfo.devfiles[0]);
		}
		close(fd);
	}
	closedir(dir);

	return devlist;
}

#endif	/* __linux__ */
