/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2021 John Tsiombikas <nuclear@member.fsf.org>

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
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "spnavd.h"
#include "logger.h"
#include "dev.h"
#include "hotplug.h"
#include "client.h"
#include "proto_unix.h"
#ifdef USE_X11
#include "proto_x11.h"
#endif

static void print_usage(const char *argv0);
static void cleanup(void);
static void redir_log(int fallback_syslog);
static void daemonize(void);
static int write_pid_file(void);
static int find_running_daemon(void);
static void handle_events(fd_set *rset);
static void sig_handler(int s);
static char *fix_path(char *str);

static char *cfgfile = DEF_CFGFILE;
static char *logfile = DEF_LOGFILE;

struct cfg cfg;
int verbose;

int main(int argc, char **argv)
{
	int i, pid, ret, become_daemon = 1;
	int force_logfile = 0;

	for(i=1; i<argc; i++) {
		if(argv[i][0] == '-') {
			if(argv[i][2] == 0) {
				switch(argv[i][1]) {
				case 'd':
					become_daemon = !become_daemon;
					break;

				case 'c':
					if(!argv[++i]) {
						fprintf(stderr, "-c must be followed by the config file name\n");
						return 1;
					}
					cfgfile = fix_path(argv[i]);
					break;

				case 'l':
					if(!argv[++i]) {
						fprintf(stderr, "-l must be followed by a logfile name or \"syslog\"\n");
						return 1;
					}
					if(strcmp(argv[i], "syslog") == 0) {
						logfile = 0;
					} else {
						logfile = fix_path(argv[i]);
						if(strcmp(logfile, argv[i]) != 0) {
							printf("logfile: %s\n", logfile);
						}
						/* when the user specifies a log file in the command line
						 * the expectation is to use it, regardless of whether
						 * spacenavd is started daemonized or not.
						 */
						force_logfile = 1;
					}
					break;

				case 'v':
					verbose = 1;
					break;

				case 'V':
					printf("spacenavd " VERSION "\n");
					return 0;

				case 'h':
					print_usage(argv[0]);
					return 0;

				default:
					fprintf(stderr, "invalid option: %s\n", argv[i]);
					return 1;
				}

			} else if(strcmp(argv[i], "-version") == 0) {
				printf("spacenavd " VERSION "\n");
				return 0;

			} else if(strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
				print_usage(argv[0]);
				return 0;

			} else {
				fprintf(stderr, "invalid option: %s\n\n", argv[i]);
				print_usage(argv[0]);
				return 1;
			}

		} else {
			fprintf(stderr, "unexpected argument: %s\n\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
	}

	if((pid = find_running_daemon()) != -1) {
		fprintf(stderr, "Spacenav daemon already running (pid: %d). Aborting.\n", pid);
		return 1;
	}

	if(become_daemon) {
		daemonize();
	} else {
		if(force_logfile) {
			redir_log(0);
		}
	}
	write_pid_file();

	logmsg(LOG_INFO, "Spacenav daemon " VERSION "\n");

	read_cfg(cfgfile, &cfg);

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGSEGV, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGUSR2, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	init_devices();
	init_hotplug();

	init_unix();
#ifdef USE_X11
	init_x11();
#endif

	atexit(cleanup);

	for(;;) {
		fd_set rset;
		int fd, max_fd = 0;
		struct client *client_iter;
		struct device *dev;

		FD_ZERO(&rset);

		dev = get_devices();
		while(dev) {
			if((fd = get_device_fd(dev)) != -1) {
				FD_SET(fd, &rset);
				if(fd > max_fd) max_fd = fd;
			}
			dev = dev->next;
		}

		if((fd = get_hotplug_fd()) != -1) {
			FD_SET(fd, &rset);
			if(fd > max_fd) max_fd = fd;
		}

		/* the UNIX domain socket listening for connections */
		if((fd = get_unix_socket()) != -1) {
			FD_SET(fd, &rset);
			if(fd > max_fd) max_fd = fd;
		}

		/* all the UNIX socket clients */
		client_iter = first_client();
		while(client_iter) {
			if(get_client_type(client_iter) == CLIENT_UNIX) {
				int s = get_client_socket(client_iter);
				assert(s >= 0);

				FD_SET(s, &rset);
				if(s > max_fd) max_fd = s;
			}
			client_iter = next_client();
		}

		/* and the X server socket */
#ifdef USE_X11
		if((fd = get_x11_socket()) != -1) {
			FD_SET(fd, &rset);
			if(fd > max_fd) max_fd = fd;
		}
#endif

		do {
			/* if there is at least one device out of the deadzone and repeat is enabled
			 * wait for only as long as specified in cfg.repeat_msec
			 */
			struct timeval tv, *timeout = 0;
			if(cfg.repeat_msec >= 0) {
				dev = get_devices();
				while(dev) {
					if(is_device_valid(dev) && !in_deadzone(dev)) {
						tv.tv_sec = cfg.repeat_msec / 1000;
						tv.tv_usec = cfg.repeat_msec % 1000;
						timeout = &tv;
						break;
					}
					dev = dev->next;
				}
			}

			ret = select(max_fd + 1, &rset, 0, 0, timeout);
		} while(ret == -1 && errno == EINTR);

		if(ret > 0) {
			handle_events(&rset);
		} else {
			if(cfg.repeat_msec >= 0) {
				dev = get_devices();
				while(dev) {
					if(!in_deadzone(dev)) {
						repeat_last_event(dev);
					}
					dev = dev->next;
				}
			}
		}
	}
	return 0;	/* unreachable */
}

static void print_usage(const char *argv0)
{
	printf("usage: %s [options]\n", argv0);
	printf("options:\n");
	printf(" -d: do not daemonize\n");
	printf(" -c <file>: config file path (default: " DEF_CFGFILE ")\n");
	printf(" -l <file>|syslog: log file path or log to syslog (default: " DEF_LOGFILE ")\n");
	printf(" -v: verbose output\n");
	printf(" -V,-version: print version number and exit\n");
	printf(" -h,-help: print usage information and exit\n");
}

static void cleanup(void)
{
	struct device *dev;

#ifdef USE_X11
	close_x11();	/* call to avoid leaving garbage in the X server's root windows */
#endif
	close_unix();

	shutdown_hotplug();

	dev = get_devices();
	while(dev) {
		struct device *tmp = dev;
		dev = dev->next;
		remove_device(tmp);
	}

	remove(PIDFILE);
}

static void redir_log(int fallback_syslog)
{
	int i, fd = -1;

	if(logfile) {
		fd = start_logfile(logfile);
	}

	if(fd >= 0 || fallback_syslog) {
		/* redirect standard input/output/error
		 * best effort attempt to make either the logfile or the syslog socket
		 * accessible through stdout/stderr, just in case any printfs survived
		 * the logmsg conversion.
		 */
		for(i=0; i<3; i++) {
			close(i);
		}

		open("/dev/zero", O_RDONLY);

		if(fd == -1) {
			fd = start_syslog(SYSLOG_ID);
			dup(1);		/* not guaranteed to work */
		} else {
			dup(fd);
		}
	}

	setvbuf(stdout, 0, _IOLBF, 0);
	setvbuf(stderr, 0, _IONBF, 0);
}

static void daemonize(void)
{
	int pid;

	chdir("/");

	redir_log(1);

	/* release controlling terminal */
	if((pid = fork()) == -1) {
		perror("failed to fork");
		exit(1);
	} else if(pid) {
		exit(0);
	}

	setsid();
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
	struct device *dev;
	struct dev_input inp;

	/* handle anything coming through the UNIX socket */
	handle_uevents(rset);

#ifdef USE_X11
	/* handle any X11 events (magellan protocol) */
	handle_xevents(rset);
#endif

	/* finally read any pending device input data */
	dev = get_devices();
	while(dev) {
		/* keep the next pointer because read_device can potentially destroy
		 * the device node if the read fails.
		 */
		struct device *next = dev->next;

		if((dev_fd = get_device_fd(dev)) != -1 && FD_ISSET(dev_fd, rset)) {
			/* read an event from the device ... */
			while(read_device(dev, &inp) != -1) {
				/* ... and process it, possibly dispatching a spacenav event to clients */
				process_input(dev, &inp);
			}
		}
		dev = next;
	}

	if((hotplug_fd = get_hotplug_fd()) != -1) {
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
	int prev_led = cfg.led;

	switch(s) {
	case SIGHUP:
		read_cfg(cfgfile, &cfg);
		if(cfg.led != prev_led) {
			struct device *dev = get_devices();
			while(dev) {
				if(is_device_valid(dev)) {
					if(verbose) {
						logmsg(LOG_INFO, "turn led %s, device: %s\n", cfg.led ? "on": "off", dev->name);
					}
					set_device_led(dev, cfg.led);
				}
				dev = dev->next;
			}
		}
		break;

	case SIGSEGV:
		logmsg(LOG_ERR, "Segmentation fault caught, trying to exit gracefully\n");
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


static char *fix_path(char *str)
{
	char *buf, *tmp;
	int sz, len;

	if(str[0] == '/') return str;

	len = strlen(str) + 1;	/* +1 for the path separator */
	sz = PATH_MAX;

	if(!(buf = malloc(sz + len))) {
		perror("failed to allocate path buffer");
		return 0;
	}

	while(!getcwd(buf, sz)) {
		if(errno == ERANGE) {
			sz *= 2;
			if(!(tmp = realloc(buf, sz + len))) {
				perror("failed to reallocate path buffer");
				free(buf);
				return 0;
			}
			buf = tmp;
		} else {
			perror("getcwd failed");
			free(buf);
			return 0;
		}
	}

	sprintf(buf + strlen(buf), "/%s", str);
	return buf;
}
