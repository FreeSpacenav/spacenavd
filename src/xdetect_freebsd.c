/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2025 John Tsiombikas <nuclear@mutantstargoat.com>

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
#if defined(__FreeBSD__) || defined(__APPLE__)

#include "config.h"

#ifdef USE_X11
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/event.h>
#include "proto_x11.h"
#include "spnavd.h"

static int kq = -1;
static int fd_x11 = -1;
static int fd_tmp = -1;

int xdet_start(void)
{
	struct timespec ts = {0, 0};
	struct kevent kev;

	if((kq = kqueue()) == -1) {
		logmsg(LOG_ERR, "failed to create kqueue: %s\n", strerror(errno));
		return -1;
	}

	if((fd_x11 = open("/tmp/.X11-unix", O_RDONLY)) == -1) {
		if((fd_tmp = open("/tmp", O_RDONLY)) == -1) {
			logmsg(LOG_ERR, "failed to open /tmp: %s\n", strerror(errno));
			goto err;
		}
	}

	EV_SET(&kev, fd_x11 != -1 ? fd_x11 : fd_tmp, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE, 0, 0);

	if(kevent(kq, &kev, 1, 0, 0, &ts) == -1) {
		logmsg(LOG_ERR, "failed to register kqueue event notification: %s\n", strerror(errno));
		goto err;
	}

	if(verbose) {
		logmsg(LOG_INFO, "waiting for the X socket file to appear\n");
	}
	return kq;

err:
	if(fd_x11 != -1)
		close(fd_x11);
	if(fd_tmp != -1)
		close(fd_tmp);
	if(kq != -1)
		close(kq);
	kq = -1;
	return -1;
}

void xdet_stop(void)
{
	if(kq != -1) {
		if(verbose) {
			logmsg(LOG_INFO, "stopping X watch\n");
		}

		if(fd_x11 != -1)
			close(fd_x11);
		if(fd_tmp != -1)
			close(fd_tmp);

		close(kq);
		kq = fd_x11 = fd_tmp = -1;
	}
}

int xdet_get_fd(void)
{
	return kq;
}

int handle_xdet_events(fd_set *rset)
{
	struct kevent kev;
	struct timespec ts = {0, 0};

	if(kq == -1 || !FD_ISSET(kq, rset)) {
		return -1;
	}

	if(kevent(kq, 0, 0, &kev, 1, &ts) <= 0) {
		return -1;
	}

	if(kev.ident == fd_tmp) {
		assert(fd_x11 == -1);

		/* try to open the socket dir, see if that was what was added to /tmp */
		if((fd_x11 = open("/tmp/.X11-unix", O_RDONLY)) == -1) {
			return -1;
		}

		EV_SET(&kev, fd_x11, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE, 0, 0);

		if(kevent(kq, &kev, 1, 0, 0, &ts) == -1) {
			logmsg(LOG_ERR, "failed to register kqueue event notification for /tmp/.X11-unix: %s\n", strerror(errno));
			close(fd_x11);
			fd_x11 = -1;
			return -1;
		}

		/* successfully added the notification for /tmp/.X11-unix, now we
		 * don't need the /tmp notification anymore. by closing the fd it's
		 * automatically removed from the kqueue.
		 */
		close(fd_tmp);
		fd_tmp = -1;

	} else if(kev.ident == fd_x11) {
		int i;

		if(verbose) {
			logmsg(LOG_INFO, "found X socket, will now attempt to connect to the X server\n");
		}

		/* poll for approximately 30 seconds (well a bit more than that) */
		for(i=0; i<30; i++) {
			sleep(1);
			if(init_x11() != -1) {
				/* done, we don't need the X socket notification any more */
				close(fd_x11);
				fd_x11 = -1;

				return 0; /* success */
			}
		}

		logmsg(LOG_ERR, "found X socket yet failed to connect\n");
	}

	return -1;
}

#endif	/* USE_X11 */

#else
int spacenavd_xdetect_freebsd_shut_up_empty_source_warning;
#endif	/* __FreeBSD__ */
