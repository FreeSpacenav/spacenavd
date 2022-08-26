/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2022 John Tsiombikas <nuclear@member.fsf.org>

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
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef USE_NETLINK
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#endif

#include "hotplug.h"
#include "dev.h"
#include "spnavd.h"
#include "cfgfile.h"

static int con_hotplug(void);
static void delay_timeout(int sig);
static void poll_timeout(int sig);

static int hotplug_fd = -1;
static int poll_time, poll_pipe = -1;
static int delay_pending, delay_pipe[2] = {-1, -1};

int init_hotplug(void)
{
	if(hotplug_fd != -1) {
		logmsg(LOG_WARNING, "WARNING: calling init_hotplug while hotplug is running!\n");
		return hotplug_fd;
	}

	if((hotplug_fd = con_hotplug()) == -1) {
		int pfd[2];

		if(verbose) {
			logmsg(LOG_WARNING, "hotplug failed will resort to polling\n");
		}

		if(pipe(pfd) == -1) {
			logmsg(LOG_ERR, "failed to open polling self-pipe: %s\n", strerror(errno));
			return -1;
		}
		poll_pipe = pfd[1];
		hotplug_fd = pfd[0];

		poll_time = 1;
		signal(SIGALRM, poll_timeout);
		alarm(poll_time);
	} else {
		if(pipe(delay_pipe) == -1) {
			logmsg(LOG_ERR, "failed to open hotplug delay self-pipe: %s\n", strerror(errno));
			return -1;
		}
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

	if(delay_pipe[0] != -1) {
		close(delay_pipe[0]);
		close(delay_pipe[1]);
		delay_pipe[0] = delay_pipe[1] = -1;
	}
}

int get_hotplug_fd(void)
{
	return delay_pending ? delay_pipe[0] : hotplug_fd;
}

int handle_hotplug(void)
{
	char buf[64];

	if(poll_pipe != -1 || delay_pending) {
		delay_pending = 0;

		read(delay_pipe[0], buf, sizeof buf);

		if(verbose > 1) {
			logmsg(LOG_DEBUG, "handle_hotplug: init_devices_usb\n");
		}

		if(init_devices_usb() == -1) {
			return -1;
		}
		return 0;
	}

	while(read(hotplug_fd, buf, sizeof buf) > 0);

	if(verbose > 1) {
		logmsg(LOG_DEBUG, "handle_hotplug: schedule delayed activation in 1 sec\n");
	}

	/* schedule a delayed trigger to avoid multiple hotplug activations in a row */
	delay_pending = 1;
	signal(SIGALRM, delay_timeout);
	alarm(1);

	return 0;
}

static int con_hotplug(void)
{
#ifdef USE_NETLINK
	int s;
	struct sockaddr_nl addr;

	if((s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT)) == -1) {
		logmsg(LOG_ERR, "failed to open hotplug netlink socket: %s\n", strerror(errno));
		return -1;
	}
	fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);

	memset(&addr, 0, sizeof addr);
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid();
	addr.nl_groups = -1;

	if(bind(s, (struct sockaddr*)&addr, sizeof addr) == -1) {
		logmsg(LOG_ERR, "failed to bind to hotplug netlink socket: %s\n", strerror(errno));
		close(s);
		return -1;
	}
	return s;
#else
	return -1;
#endif	/* USE_NETLINK */
}

static void delay_timeout(int sig)
{
	write(delay_pipe[1], &sig, 1);
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



#else
int spacenavd_hotplug_linux_shut_up_empty_source_warning;
#endif	/* __linux__ */
