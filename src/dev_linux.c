/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2010 John Tsiombikas <nuclear@member.fsf.org>

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
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#ifdef USE_NETLINK
#include <linux/netlink.h>
#endif
#include <linux/types.h>
#include <linux/input.h>
#include "dev.h"
#include "cfgfile.h"
#include "spnavd.h"
#include "dev_serial.h"

#define DEV_POLL_INTERVAL	30


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


static int open_dev_usb(const char *path);
static int read_dev_usb(struct dev_input *inp);
static char *get_dev_path(void);
static int con_hotplug(void);
static void poll_timeout(int sig);

static int dev_fd = -1;
static char dev_name[128];
static unsigned char evtype_mask[(EV_MAX + 7) / 8];
#define TEST_BIT(b, ar)	(ar[b / 8] & (1 << (b % 8)))

static int hotplug_fd = -1;
static int poll_time, poll_pipe;
#define MAX_POLL_TIME	30

static int dev_is_serial;

/* hotplug stuff */

int init_hotplug(void)
{
	if((hotplug_fd = con_hotplug()) == -1) {
		int pfd[2];

		if(verbose) {
			printf("hotplug failed will resort to polling\n");
		}

		if(pipe(pfd) == -1) {
			perror("failed to open polling self-pipe");
			return -1;
		}
		poll_pipe = pfd[1];
		hotplug_fd = pfd[0];

		poll_time = 1;
		signal(SIGALRM, poll_timeout);
		alarm(poll_time);
	}

	return hotplug_fd;
}

void shutdown_hotplug(void)
{
	if(hotplug_fd != -1) {
		close(hotplug_fd);
		hotplug_fd = -1;
	}

	if(poll_pipe != -1) {
		close(poll_pipe);
		poll_pipe = -1;
	}
}

int get_hotplug_fd(void)
{
	return hotplug_fd;
}

int handle_hotplug(void)
{
	char buf[512];
	read(hotplug_fd, buf, sizeof buf);

	if(dev_fd == -1) {
		if(init_dev() == -1) {
			return -1;
		}
		shutdown_hotplug();
	}

	return 0;
}

static int con_hotplug(void)
{
	int s = -1;

#ifdef USE_NETLINK
	struct sockaddr_nl addr;

	if((s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT)) == -1) {
		perror("failed to open hotplug netlink socket");
		return -1;
	}

	memset(&addr, 0, sizeof addr);
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid();
	addr.nl_groups = -1;

	if(bind(s, (struct sockaddr*)&addr, sizeof addr) == -1) {
		perror("failed to bind to hotplug netlink socket");
		close(s);
		return -1;
	}
#endif	/* USE_NETLINK */

	return s;
}

static void poll_timeout(int sig)
{
	signal(sig, poll_timeout);

	if(sig == SIGALRM) {
		if(poll_pipe != -1) {
			write(poll_pipe, &sig, 1);
			poll_time *= 2;
			alarm(poll_time);
		}
	}
}


/* device handling */

int init_dev(void)
{
	if(cfg.serial_dev[0]) {
		/* try to open a serial device if specified in the config file */
		printf("using device: %s\n", cfg.serial_dev);

		if((dev_fd = open_dev_serial(cfg.serial_dev)) == -1) {
			return -1;
		}
		dev_is_serial = 1;

	} else {
		char *dev_path;
		if(!(dev_path = get_dev_path())) {
			fprintf(stderr, "failed to find the spaceball device file\n");
			return -1;
		}
		printf("using device: %s\n", dev_path);

		if((dev_fd = open_dev_usb(dev_path)) == -1) {
			return -1;
		}
		dev_is_serial = 0;

		printf("device name: %s\n", dev_name);
	}
	return 0;
}

void shutdown_dev(void)
{
	if(dev_is_serial) {
		close_dev_serial();
	} else {
		if(dev_fd != -1) {
			set_led(0);
			close(dev_fd);
		}
	}
	dev_fd = -1;
}

int get_dev_fd(void)
{
	return dev_fd;
}

int read_dev(struct dev_input *inp)
{
	return dev_is_serial ? read_dev_serial(inp) : read_dev_usb(inp);
}

static int read_dev_usb(struct dev_input *inp)
{
	struct input_event iev;
	int rdbytes;

	if(dev_fd == -1) {
		return -1;
	}

	do {
		rdbytes = read(dev_fd, &iev, sizeof iev);
	} while(rdbytes == -1 && errno == EINTR);

	/* disconnect? */
	if(rdbytes == -1) {
		if(errno != EAGAIN) {
			perror("read error");
			close(dev_fd);
			dev_fd = -1;

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

void set_led(int state)
{
	struct input_event ev;

	if(dev_fd == -1) {
		fprintf(stderr, "set_led failed, invalid dev_fd\n");
		return;
	}

	memset(&ev, 0, sizeof ev);
	ev.type = EV_LED;
	ev.code = LED_MISC;
	ev.value = state;

	if(write(dev_fd, &ev, sizeof ev) == -1) {
		fprintf(stderr, "failed to turn LED %s\n", state ? "on" : "off");
	}
}

static int open_dev_usb(const char *path)
{
	int grab = 1;

	if((dev_fd = open(path, O_RDWR)) == -1) {
		if((dev_fd = open(path, O_RDONLY)) == -1) {
			perror("failed to open device");
			return -1;
		}
		fprintf(stderr, "opened device read-only, LEDs won't work\n");
	}

	if(ioctl(dev_fd, EVIOCGNAME(sizeof(dev_name)), dev_name) == -1) {
		perror("EVIOCGNAME ioctl failed\n");
		strcpy(dev_name, "unknown");
	}

	if(ioctl(dev_fd, EVIOCGBIT(0, sizeof(evtype_mask)), evtype_mask) == -1) {
		perror("EVIOCGBIT ioctl failed\n");
		close(dev_fd);
		return -1;
	}

	/* try to grab the device */
	if(ioctl(dev_fd, EVIOCGRAB, &grab) == -1) {
		perror("failed to grab the spacenav device");
	}

	/* set non-blocking */
	fcntl(dev_fd, F_SETFL, fcntl(dev_fd, F_GETFL) | O_NONBLOCK);

	if(cfg.led) {
		set_led(1);
	}
	return dev_fd;
}


#define PROC_DEV	"/proc/bus/input/devices"
static char *get_dev_path(void)
{
	static char path[128];
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
