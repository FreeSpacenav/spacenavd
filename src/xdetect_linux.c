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
#ifdef __linux__

#include "config.h"

#ifdef USE_X11
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include "proto_x11.h"
#include "spnavd.h"

/* TODO implement fallback to polling if inotify is not available */

static int fd = -1;
static int watch_tmp = -1, watch_x11 = -1;

int xdet_start(void)
{
	if((fd = inotify_init()) == -1) {
		perror("failed to create inotify queue");
		return -1;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	if((watch_x11 = inotify_add_watch(fd, "/tmp/.X11-unix", IN_CREATE)) == -1) {
		if((watch_tmp = inotify_add_watch(fd, "/tmp", IN_CREATE)) == -1) {
			perror("failed to watch /tmp for file events");
			close(fd);
			fd = -1;
			return -1;
		}
	}

	if(verbose) {
		printf("waiting for the X socket file to appear\n");
	}

	return fd;
}

/* this is called by init_x11 if it's successful */
void xdet_stop(void)
{
	if(fd != -1) {
		if(verbose) {
			printf("stopping X watch\n");
		}

		close(fd);
		fd = watch_tmp = watch_x11 = -1;
	}
}

int xdet_get_fd(void)
{
	return fd;
}

int handle_xdet_events(fd_set *rset)
{
	char buf[512];
	struct inotify_event *ev = (struct inotify_event*)buf;
	ssize_t res;

	if(fd == -1 || !FD_ISSET(fd, rset)) {
		return -1;
	}

	for(;;) {
		if((res = read(fd, buf, sizeof buf)) <= 0) {
			if(res == 0) {
				/* kernels before 2.6.14 returned 0 for not enough space */
				errno = EINVAL;
			}
			if(errno == EINTR) continue;
			if(errno != EAGAIN) {
				perror("failed to read inotify event");
			}
			return -1;
		}

		if(ev->wd == watch_tmp) {
			if(watch_x11 != -1) {
				inotify_rm_watch(fd, watch_tmp);
				continue;
			}

			if(ev->len > 0 && strcmp(ev->name, ".X11-unix") == 0) {
				if((watch_x11 = inotify_add_watch(fd, "/tmp/.X11-unix", IN_CREATE)) == -1) {
					perror("failed to add /tmp/.X11-unix to the watch queue");
					continue;
				}
			}

		} else if(ev->wd == watch_x11) {
			char *dpystr, sock_file[64];
			int dpynum = 0;

			if((dpystr = getenv("DISPLAY"))) {
				char *tmp = strchr(dpystr, ':');
				if(tmp && isdigit(tmp[1])) {
					dpynum = atoi(tmp + 1);
				}
			}
			sprintf(sock_file, "X%d", dpynum);

			if(ev->len > 0 && strcmp(ev->name, sock_file) == 0) {
				int i;

				if(verbose) {
					printf("found X socket, will now attempt to connect to the X server\n");
				}

				/* poll for approximately 30 seconds (well a bit more than that) */
				for(i=0; i<30; i++) {
					sleep(1);
					if(init_x11() != -1) {
						return 0; /* success */
					}
				}

				fprintf(stderr, "found X socket yet failed to connect\n");
			}
		}
	}

	return -1;
}
#endif	/* USE_X11 */

#else
int spacenavd_xdetect_linux_shut_up_empty_source_warning;
#endif	/* __linux__ */
