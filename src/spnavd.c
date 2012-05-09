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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "spnavd.h"
#include "dev.h"
#include "hotplug.h"
#include "client.h"
#include "proto_unix.h"
#ifdef USE_X11
#include "proto_x11.h"
#endif

static void cleanup(void);
static void daemonize(void);
static int write_pid_file(void);
static int find_running_daemon(void);
static void handle_events(fd_set *rset);
static void sig_handler(int s);


int main(int argc, char **argv)
{
	int i, pid, ret, become_daemon = 1;

	for(i=1; i<argc; i++) {
		if(argv[i][0] == '-' && argv[i][2] == 0) {
			switch(argv[i][1]) {
			case 'd':
				become_daemon = !become_daemon;
				break;

			case 'v':
				verbose = 1;
				break;

			case 'h':
				printf("usage: %s [options]\n", argv[0]);
				printf("options:\n");
				printf("  -d\tdo not daemonize\n");
				printf("  -v\tverbose output\n");
				printf("  -h\tprint this usage information\n");
				return 0;

			default:
				fprintf(stderr, "unrecognized argument: %s\n", argv[i]);
				return 1;
			}
		} else {
			fprintf(stderr, "unexpected argument: %s\n", argv[i]);
			return 1;
		}
	}

	if((pid = find_running_daemon()) != -1) {
		fprintf(stderr, "Spacenav daemon already running (pid: %d). Aborting.\n", pid);
		return 1;
	}

	if(become_daemon) {
		daemonize();
	}
	write_pid_file();

	puts("Spacenav daemon " VERSION);

	read_cfg("/etc/spnavrc", &cfg);

	if(init_clients() == -1) {
		return 1;
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGSEGV, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGUSR2, sig_handler);

	if(init_dev() == -1) {
		init_hotplug();
	}
	init_unix();
#ifdef USE_X11
	init_x11();
#endif

	atexit(cleanup);

	for(;;) {
		fd_set rset;
		int fd, max_fd = 0;
		struct client *c;

		FD_ZERO(&rset);

		/* set the device fd if it's open, otherwise set the hotplug fd */
		if((fd = get_dev_fd()) != -1 || (fd = get_hotplug_fd()) != -1) {
			FD_SET(fd, &rset);
			if(fd > max_fd) max_fd = fd;
		}

		/* the UNIX domain socket listening for connections */
		if((fd = get_unix_socket()) != -1) {
			FD_SET(fd, &rset);
			if(fd > max_fd) max_fd = fd;
		}

		/* all the UNIX socket clients */
		c = first_client();
		while(c) {
			if(get_client_type(c) == CLIENT_UNIX) {
				int s = get_client_socket(c);
				assert(s >= 0);

				FD_SET(s, &rset);
				if(s > max_fd) max_fd = s;
			}
			c = next_client();
		}

		/* and the X server socket */
#ifdef USE_X11
		if((fd = get_x11_socket()) != -1) {
			FD_SET(fd, &rset);
			if(fd > max_fd) max_fd = fd;
		}
#endif

		do {
			struct timeval tv, *timeout = 0;
			if(is_dev_valid() && cfg.repeat_msec >= 0 && !in_deadzone()) {
				tv.tv_sec = cfg.repeat_msec / 1000;
				tv.tv_usec = cfg.repeat_msec % 1000;
				timeout = &tv;
			}

			ret = select(max_fd + 1, &rset, 0, 0, timeout);
		} while(ret == -1 && errno == EINTR);

		if(ret > 0) {
			handle_events(&rset);
		} else {
			if(cfg.repeat_msec >= 0 && !in_deadzone()) {
				repeat_last_event();
			}
		}
	}
	return 0;	/* unreachable */
}

static void cleanup(void)
{
#ifdef USE_X11
	close_x11();	/* call to avoid leaving garbage in the X server's root windows */
#endif
	close_unix();
	shutdown_dev();
	remove(PIDFILE);
}

static void daemonize(void)
{
	int i, pid;

	if((pid = fork()) == -1) {
		perror("failed to fork");
		exit(1);
	} else if(pid) {
		exit(0);
	}

	setsid();
	chdir("/");

	/* redirect standard input/output/error */
	for(i=0; i<3; i++) {
		close(i);
	}

	open("/dev/zero", O_RDONLY);
	if(open(LOGFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644) == -1) {
		open("/dev/null", O_WRONLY);
	}
	dup(1);

	setvbuf(stdout, 0, _IOLBF, 0);
	setvbuf(stderr, 0, _IONBF, 0);
}

static int write_pid_file(void)
{
	FILE *fp;
	int pid = getpid();

	if(!(fp = fopen(PIDFILE, "w"))) {
		return -1;
	}
	fprintf(fp, "%d\n", pid);
	fclose(fp);
	return 0;
}

static int find_running_daemon(void)
{
	FILE *fp;
	int s, pid;
	struct sockaddr_un addr;

	/* try to open the pid-file */
	if(!(fp = fopen(PIDFILE, "r"))) {
		return -1;
	}
	if(fscanf(fp, "%d\n", &pid) != 1) {
		fclose(fp);
		return -1;
	}
	fclose(fp);

	/* make sure it's not just a stale pid-file */
	if((s = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		return -1;
	}
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, SOCK_NAME, sizeof addr.sun_path);

	if(connect(s, (struct sockaddr*)&addr, sizeof addr) == -1) {
		close(s);
		return -1;
	}

	/* managed to connect alright, it's running... */
	close(s);
	return pid;
}

static void handle_events(fd_set *rset)
{
	int dev_fd, hotplug_fd;

	/* handle anything coming through the UNIX socket */
	handle_uevents(rset);

#ifdef USE_X11
	/* handle any X11 events (magellan protocol) */
	handle_xevents(rset);
#endif

	/* finally read any pending device input data */
	if((dev_fd = get_dev_fd()) != -1) {
		if(FD_ISSET(dev_fd, rset)) {
			struct dev_input inp;

			/* read an event from the device ... */
			while(read_dev(&inp) != -1) {
				/* ... and process it, possibly dispatching a spacenav event to clients */
				process_input(&inp);
			}
		}

	} else if((hotplug_fd = get_hotplug_fd()) != -1) {
		if(FD_ISSET(hotplug_fd, rset)) {
			handle_hotplug();
		}
	}
}

/* signals usr1 & usr2 are sent by the spnav_x11 script to start/stop the
 * daemon's connection to the X server.
 */
static void sig_handler(int s)
{
	int tmp;

	switch(s) {
	case SIGHUP:
		tmp = cfg.led;
		read_cfg("/etc/spnavrc", &cfg);
		if(cfg.led != tmp && get_dev_fd() >= 0) {
			set_led(cfg.led);
		}
		break;

	case SIGSEGV:
		fprintf(stderr, "Segmentation fault caught, trying to exit gracefully\n");
	case SIGINT:
	case SIGTERM:
		exit(0);

#ifdef USE_X11
	case SIGUSR1:
		init_x11();
		break;

	case SIGUSR2:
		close_x11();
		break;
#endif

	default:
		break;
	}
}

