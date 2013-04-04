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
#include <signal.h>
#include <unistd.h>

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
static void poll_timeout(int sig);

static int hotplug_fd = -1;
static int poll_time, poll_pipe;

int init_hotplug(void)
{
	if(hotplug_fd != -1) {
		fprintf(stderr, "WARNING: calling init_hotplug while hotplug is running!\n");
		return hotplug_fd;
	}

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

	if(verbose)
		printf("\nhandle_hotplug called\n");

	if (init_devices() == -1)
		return -1;

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



#endif	/* __linux__ */
