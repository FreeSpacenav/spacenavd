/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007 John Tsiombikas <nuclear@siggraph.org>

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

#include <linux/types.h>
#include <linux/input.h>

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


#ifdef USE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>

enum cmd_msg {
	CMD_NONE,
	CMD_APP_WINDOW = 27695,	/* set client window */
	CMD_APP_SENS			/* set app sensitivity */
};
#endif

/* client types */
enum {CLIENT_X11, CLIENT_UNIX};

struct client {
	int type;

	int sock;		/* UNIX domain socket */
#ifdef USE_X11
	Window win;		/* X11 client window */
#endif

	float sens;			/* sensitivity */

	struct client *next;
};


void daemonize(void);
int select_all(fd_set *rd_set);
void handle_events(fd_set *rd_set);
int add_client(int type, void *cdata);
int init_dev(void);
int init_unix(void);
void send_uevent(struct input_event *inp);

#ifdef USE_X11
int init_x11(void);
void close_x11(void);
void send_xevent(struct input_event *inp);
int catch_badwin(Display *dpy, XErrorEvent *err);
void set_client_window(Window win);
void remove_client_window(Window win);
#endif

char *get_dev_path(void);
int open_dev(const char *path);
void close_dev(void);
void set_led(int state);
void sig_handler(int s);

unsigned int msec_dif(struct timeval tv1, struct timeval tv2);

int read_cfg(const char *fname);


int dev_fd;
char dev_name[128];
unsigned char evtype_mask[(EV_MAX + 7) / 8];
#define TEST_BIT(b, ar)	(ar[b / 8] & (1 << (b % 8)))

int evrel[6];
int evbut[24];

int lsock;

#ifdef USE_X11
Display *dpy;
Window win;
Atom event_motion, event_bpress, event_brelease, event_cmd;

float x11_sens = 1.0;	/* XXX This stands in for the client sensitivity. Due
						 * to the bad design of the original magellan protocol,
						 * we can't know which client requested the sensitivity
						 * change, so we have to keep it global for all X
						 * clients.
						 */
#endif

struct client *client_list;

/* global configuration options */
float sensitivity = 1.0;
int dead_threshold = 2;


int main(int argc, char **argv)
{
	int i, become_daemon = 1;

	for(i=1; i<argc; i++) {
		if(argv[i][0] == '-' && argv[i][2] == 0) {
			switch(argv[i][1]) {
			case 'd':
				become_daemon = !become_daemon;
				break;

			case 'h':
				printf("usage: %s [options]\n", argv[0]);
				printf("options:\n");
				printf("  -d\tdo not daemonize\n");
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

	if(become_daemon) {
		daemonize();
	}

	read_cfg("/etc/spnavrc");

	if(!(client_list = malloc(sizeof *client_list))) {
		perror("failed to allocate client list");
		return 1;
	}
	client_list->next = 0;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGSEGV, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGUSR2, sig_handler);

	/* initialize the input device and the X11 connection (if available) */
	if(init_dev() == -1) {
		return 1;
	}
	init_unix();
#ifdef USE_X11
	init_x11();
#endif

	/* event handling loop */
	while(1) {
		fd_set rd_set;

		if(dev_fd == -1) {
			if(init_dev() == -1) {
				sleep(30);
				continue;
			}
		}

		if(select_all(&rd_set) >= 0) {
			handle_events(&rd_set);
		}
	}

	/* just for the sense of symmetry, execution can't reach this :) */
#ifdef USE_X11
	close_x11();
#endif
	close_dev();
	return 0;
}

void daemonize(void)
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
	if(open("/tmp/spnav.log", O_WRONLY | O_CREAT | O_TRUNC, 0644) == -1) {
		open("/dev/null", O_WRONLY);
	}
	dup(1);

	setvbuf(stdout, 0, _IOLBF, 0);
	setvbuf(stderr, 0, _IONBF, 0);
}

int select_all(fd_set *rd_set)
{
	int ret;
	int max_fd, xcon;
	struct client *cptr;

	FD_ZERO(rd_set);

	/* set the device file descriptors */
	FD_SET(dev_fd, rd_set);
	max_fd = dev_fd;

	/* if we have a listening socket... */
	if(lsock) {
		/* ... set the listening socket itself for incoming connections */
		FD_SET(lsock, rd_set);
		if(lsock > max_fd) max_fd = lsock;

		/* ... and set all the client's sockets too */
		cptr = client_list->next;
		while(cptr) {
			if(cptr->type == CLIENT_UNIX) {
				FD_SET(cptr->sock, rd_set);

				if(cptr->sock > max_fd) {
					max_fd = cptr->sock;
				}
			}
			cptr = cptr->next;
		}
	}

	/* also if we have an X11 connection, select that as well */
#ifdef USE_X11
	if(dpy) {
		xcon = ConnectionNumber(dpy);
		FD_SET(xcon, rd_set);
		
		if(xcon > max_fd) {
			max_fd = xcon;
		}
	}
#endif

	/* wait indefinitely */
	do {
		ret = select(max_fd + 1, rd_set, 0, 0, 0);
	} while(ret == -1 && errno == EINTR);

	return ret;
}

void handle_events(fd_set *rd_set)
{
	int rdbytes = 0;
	struct input_event inp;
	static struct input_event prev_inp;
	static int inp_events_pending;
	struct client *cptr;

	if(lsock) {
		if(FD_ISSET(lsock, rd_set)) {
			/* got incoming connection */
			int s;

			if((s = accept(lsock, 0, 0)) == -1) {
				perror("error while accepting connection");
			} else {
				if(add_client(CLIENT_UNIX, &s) == -1) {
					perror("failed to add client");
				}
			}
		}

		/* check all UNIX clients */
		cptr = client_list;
		while(cptr->next) {
			struct client *client = cptr->next;

			if(client->type == CLIENT_UNIX) {
				if(FD_ISSET(client->sock, rd_set)) {
					/* Got data from a client. Currently the only thing the client may set
					 * is sensitivity. So get it directly.
					 */
					while((rdbytes = read(client->sock, &client->sens, sizeof client->sens)) <= 0 && errno == EINTR);
					if(rdbytes <= 0) {	/* something went wrong... disconnect client */
						cptr->next = client->next;
						close(client->sock);
						free(client);
						continue;
					}
				}
			}
			cptr = cptr->next;
		}
	}

#ifdef USE_X11
	/* process any pending X events */
	if(dpy && FD_ISSET(ConnectionNumber(dpy), rd_set)) {
		while(XPending(dpy)) {
			XEvent xev;
			XNextEvent(dpy, &xev);

			if(xev.type == ClientMessage && xev.xclient.message_type == event_cmd) {
				unsigned int win_id;

				switch(xev.xclient.data.s[2]) {
				case CMD_APP_WINDOW:
					win_id = xev.xclient.data.s[1];
					win_id |= (unsigned int)xev.xclient.data.s[0] << 16;

					set_client_window((Window)win_id);
					break;

				case CMD_APP_SENS:
					x11_sens = *(float*)xev.xclient.data.s;	/* see decl of x11_sens for details */
					break;

				default:
					break;
				}
			}
		}
	}
#endif

	/* read any pending data from the device */
	if(FD_ISSET(dev_fd, rd_set)) {
		do {
			rdbytes = read(dev_fd, &inp, sizeof inp);
		} while(rdbytes == -1 && errno == EINTR);
	}

	/* disconnect? */
	if(rdbytes == -1) {
		perror("read error");
		close(dev_fd);
		dev_fd = -1;
	}

	/* if we actually got an event, update our state, and send the appropriate events to all clients */
	if(rdbytes > 0) {
		int val;
		int idx, sign = -1;

		switch(inp.type) {
		case EV_REL:
			if(abs(inp.value) < dead_threshold) {
				break;
			}

			idx = inp.code - REL_X;
			val = inp.value;
			if(sensitivity != 1.0) {
				val = (int)((float)inp.value * sensitivity);
			}

			switch(idx) {
			case REL_RY:
				idx = REL_RZ; break;
			case REL_RZ:
				idx = REL_RY; break;
			case REL_Y:
				idx = REL_Z; break;
			case REL_Z:
				idx = REL_Y; break;
			default:
				sign = 1;
				break;
			}

			evrel[idx] = sign * val;
			prev_inp = inp;
			inp_events_pending = 1;
			break;

		case EV_KEY:
			idx = inp.code - BTN_0;
			evbut[idx] = inp.value;
			prev_inp = inp;
			inp_events_pending = 1;
			break;

		case EV_SYN:
			if(inp_events_pending) {
				inp_events_pending = 0;

				send_uevent(&prev_inp);
#ifdef USE_X11
				/* if we are connected to an X server, send the appropriate X event */
				send_xevent(&prev_inp);
#endif/* USE_X11 */
			}
			break;

		default:
			break;
		}

	}

}

int add_client(int type, void *cdata)
{
	struct client *client;

#ifdef USE_X11
	if(!cdata || (type != CLIENT_UNIX && type != CLIENT_X11)) 
#else
	if(!cdata || type != CLIENT_UNIX) 
#endif
	{
		return -1;
	}

	if(!(client = malloc(sizeof *client))) {
		return -1;
	}

	client->type = type;
	if(type == CLIENT_UNIX) {
		client->sock = *(int*)cdata;
#ifdef USE_X11
	} else {
		client->win = *(Window*)cdata;
#endif
	}

	client->sens = 1.0f;
	client->next = client_list->next;
	client_list->next = client;

	return 0;
}

int init_dev(void)
{
	char *dev_path;

	if(!(dev_path = get_dev_path())) {
		fprintf(stderr, "failed to find the spaceball device file\n");
		return -1;
	}
	printf("using device: %s\n", dev_path);

	if(open_dev(dev_path) == -1) {
		return -1;
	}
	printf("device name: %s\n", dev_name);

	return 0;
}


#define SOCK_NAME	"/tmp/.spnav.sock"
int init_unix(void)
{
	int s;
	mode_t prev_umask;
	struct sockaddr_un addr;

	if(lsock) return 0;

	if((s = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("failed to create socket");
		return -1;
	}

	unlink(SOCK_NAME);	/* in case it already exists */

	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, SOCK_NAME);

	prev_umask = umask(0);

	if(bind(s, (struct sockaddr*)&addr, sizeof addr) == -1) {
		fprintf(stderr, "failed to bind unix socket: %s: %s\n", SOCK_NAME, strerror(errno));
		return -1;
	}

	umask(prev_umask);

	if(listen(s, 8) == -1) {
		perror("listen failed");
		return -1;
	}

	lsock = s;
	return 0;
}


enum {
	UEV_TYPE_MOTION,
	UEV_TYPE_PRESS,
	UEV_TYPE_RELEASE
};

/* send an event to all UNIX clients */
void send_uevent(struct input_event *inp)
{
	static struct timeval prev_motion_time;
	struct client *citer;
	int i, data[8];
	unsigned int period;

	if(!lsock) return;

	if(inp->type == EV_REL) {
		period = msec_dif(inp->time, prev_motion_time);
		prev_motion_time = inp->time;
	}

	citer = client_list->next;
	while(citer) {
		if(citer->type == CLIENT_UNIX) {
			float motion_mul;

			switch(inp->type) {
			case EV_REL:
				data[0] = UEV_TYPE_MOTION;

				motion_mul = citer->sens * sensitivity;
				for(i=0; i<6; i++) {
					float val = (float)evrel[i] * motion_mul;
					data[i + 1] = (int)val;
				}
				data[7] = period;
				break;

			case EV_KEY:
				data[0] = inp->value ? UEV_TYPE_PRESS : UEV_TYPE_RELEASE;
				data[1] = inp->code - BTN_0;
				break;

			default:
				fprintf(stderr, "BUG! this shouldn't happen\n");
				exit(1);
			}

			while(write(citer->sock, data, sizeof data) == -1 && errno == EINTR);
		}
		citer = citer->next;
	}
}


#ifdef USE_X11
int init_x11(void)
{
	int i, screen, scr_count;
	Window root;
	XSetWindowAttributes xattr;
	Atom wm_delete, cmd_type;
	XTextProperty tp_wname;
	XClassHint class_hint;
	char *win_title = "Magellan Window";

	if(dpy) return 0;

	if(!(dpy = XOpenDisplay(":0"))) {
		fprintf(stderr, "failed to open X11 display: \":0\"\n");
		return -1;
	}
	scr_count = ScreenCount(dpy);
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	/* intern the various atoms used for communicating with the magellan clients */
	event_motion = XInternAtom(dpy, "MotionEvent", False);
	event_bpress = XInternAtom(dpy, "ButtonPressEvent", False);
	event_brelease = XInternAtom(dpy, "ButtonReleaseEvent", False);
	event_cmd = XInternAtom(dpy, "CommandEvent", False);

	/* Create a dummy window, so that clients are able to send us events
	 * through the magellan API. No need to map the window.
	 */
	xattr.background_pixel = xattr.border_pixel = BlackPixel(dpy, screen);
	xattr.colormap = DefaultColormap(dpy, screen);

	win = XCreateWindow(dpy, root, 0, 0, 10, 10, 0, CopyFromParent, InputOutput,
			DefaultVisual(dpy, screen), CWColormap | CWBackPixel | CWBorderPixel, &xattr);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	XStringListToTextProperty(&win_title, 1, &tp_wname);
	XSetWMName(dpy, win, &tp_wname);
	XFree(tp_wname.value);

	class_hint.res_name = "magellan";
	class_hint.res_class = "magellan_win";
	XSetClassHint(dpy, win, &class_hint);

	/* I believe this is a bit hackish, but the magellan API expects to find the CommandEvent
	 * property on the root window, containing our window id.
	 * The API doesn't look for a specific property type, so I made one up here (MagellanCmdType).
	 */
	cmd_type = XInternAtom(dpy, "MagellanCmdType", False);
	for(i=0; i<scr_count; i++) {
		Window root = RootWindow(dpy, i);
		XChangeProperty(dpy, root, event_cmd, cmd_type, 32, PropModeReplace, (unsigned char*)&win, 1);
	}
	XFlush(dpy);

	return 0;
}

void close_x11(void)
{
	int i, scr_count;

	if(!dpy) return;

	/* first delete all the CommandEvent properties from all root windows */
	scr_count = ScreenCount(dpy);
	for(i=0; i<scr_count; i++) {
		Window root = RootWindow(dpy, i);
		XDeleteProperty(dpy, root, event_cmd);
	}

	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	dpy = 0;
}

void send_xevent(struct input_event *inp)
{
	static struct timeval prev_motion_time;
	struct client *citer;
	int (*prev_xerr_handler)(Display*, XErrorEvent*);
	int i;
	XEvent xevent;
	unsigned int period;

	if(!dpy) return;

	if(inp->type == EV_REL) {
		period = msec_dif(inp->time, prev_motion_time);
		prev_motion_time = inp->time;
	}

	/* We can get a BadWindow exception, if any of the registered clients exit
	 * without notice. Thus we must install a custom handler to avoid crashing
	 * the daemon when that happens. Also catch_badwin (see below) removes that
	 * client from the list, to avoid perpetually trying to send events to an
	 * invalid window.
	 */
	prev_xerr_handler = XSetErrorHandler(catch_badwin);

	xevent.type = ClientMessage;
	xevent.xclient.send_event = False;
	xevent.xclient.display = dpy;

	citer = client_list->next;
	while(citer) {
		if(citer->type != CLIENT_X11) {
			citer = citer->next;
			continue;
		}

		xevent.xclient.window = citer->win;

		switch(inp->type) {
		case EV_REL:
			xevent.xclient.message_type = event_motion;
			xevent.xclient.format = 16;

			for(i=0; i<6; i++) {
				float val = (float)evrel[i] * x11_sens * sensitivity;
				xevent.xclient.data.s[i + 2] = (short)val;
			}
			xevent.xclient.data.s[0] = xevent.xclient.data.s[1] = 0;
			xevent.xclient.data.s[8] = period;
			break;

		case EV_KEY:
			xevent.xclient.message_type = inp->value ? event_bpress : event_brelease;
			xevent.xclient.format = 16;
			xevent.xclient.data.s[2] = inp->code - BTN_0;
			break;

		default:
			fprintf(stderr, "BUG! this shouldn't happen\n");
			exit(1);
		}

		XSendEvent(dpy, citer->win, False, 0, &xevent);
		citer = citer->next;
	}

	XSync(dpy, False);
	XSetErrorHandler(prev_xerr_handler);
}

/* X11 error handler for bad-windows */
int catch_badwin(Display *dpy, XErrorEvent *err)
{
	char buf[256];

	if(err->error_code == BadWindow) {
		remove_client_window((Window)err->resourceid);
	} else {
		XGetErrorText(dpy, err->error_code, buf, sizeof buf);
		fprintf(stderr, "Caught unexpected X error: %s\n", buf);
	}
	return 0;
}


/* adds a new X11 client to the list, IF it does not already exist */
void set_client_window(Window win)
{
	int i, scr_count;
	struct client *cnode;

	/* When a magellan application exits, the SDK sets another window to avoid
	 * crashing the original proprietary daemon.  The new free SDK will set
	 * consistently the root window for that purpose, which we can ignore here
	 * easily.
	 */
	scr_count = ScreenCount(dpy);
	for(i=0; i<scr_count; i++) {
		if(win == RootWindow(dpy, i)) {
			return;
		}
	}

	cnode = client_list->next;
	while(cnode) {
		if(cnode->win == win) {
			return;
		}
		cnode = cnode->next;
	}

	add_client(CLIENT_X11, &win);
}

void remove_client_window(Window win)
{
	struct client *cnode, *tmp;

	cnode = client_list;
	while(cnode->next) {
		if(cnode->next->win == win) {
			tmp = cnode->next;
			cnode->next = tmp->next;
			free(tmp);
			return;
		}
		cnode = cnode->next;
	}
}
#endif

int open_dev(const char *path)
{
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
		return -1;
	}

	if(!TEST_BIT(EV_REL, evtype_mask)) {
		fprintf(stderr, "Wrong device, no relative events reported!\n");
		return -1;
	}

	set_led(1);
	return 0;
}

void close_dev(void)
{
	if(dev_fd != -1) {
		set_led(0);
	}
	close(dev_fd);
	dev_fd = -1;
}

void set_led(int state)
{
	struct input_event ev;

	if(dev_fd == -1) {
		fprintf(stderr, "set_led failed, invalid dev_fd\n");
		return;
	}

	ev.type = EV_LED;
	ev.code = LED_MISC;
	ev.value = state;

	if(write(dev_fd, &ev, sizeof ev) == -1) {
		fprintf(stderr, "failed to turn LED %s\n", state ? "on" : "off");
	}
}


#define PROC_DEV	"/proc/bus/input/devices"
char *get_dev_path(void)
{
	static char path[128];
	int valid_vendor = 0, valid_str = 0;
	char buf[1024];
	FILE *fp;

	if(!(fp = fopen(PROC_DEV, "r"))) {
		perror("failed to open " PROC_DEV ":");
		return 0;
	}

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

				if((ptr = strchr(start, ' '))) {
					*ptr = 0;
				}
				if((ptr = strchr(start, '\n'))) {
					*ptr = 0;
				}

				snprintf(path, sizeof(path), "/dev/input/%s", start);
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
	return 0;
}


/* signals usr1 & usr2 are sent by the spnav_x11 script to start/stop the
 * daemon's connection to the X server.
 */
void sig_handler(int s)
{
	switch(s) {
	case SIGHUP:
		init_dev();
		break;

	case SIGSEGV:
		fprintf(stderr, "Segmentation fault caught, trying to exit gracefully\n");
	case SIGINT:
	case SIGTERM:
		close_x11();	/* call to avoid leaving garbage in the X server's root windows */
		close_dev();
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

unsigned int msec_dif(struct timeval tv1, struct timeval tv2)
{
	unsigned int ds, du;

	ds = tv2.tv_sec - tv1.tv_sec;
	du = tv2.tv_usec - tv1.tv_usec;
	return ds * 1000 + du / 1000;
}

int read_cfg(const char *fname)
{
	FILE *fp;
	char buf[512];

	if(!(fp = fopen(fname, "r"))) {
		fprintf(stderr, "failed to open config file %s: %s. using defaults.\n", fname, strerror(errno));
		return -1;
	}

	while(fgets(buf, sizeof buf, fp)) {
		char *key_str, *val_str, *line = buf;
		while(*line == ' ' || *line == '\t') line++;

		if(!*line || *line == '\n' || *line == '\r' || *line == '#') {
			continue;
		}

		if(!(key_str = strtok(line, " :=\n\t\r"))) {
			fprintf(stderr, "invalid config line: %s, skipping.\n", line);
			continue;
		}
		if(!(val_str = strtok(0, " :=\n\t\r"))) {
			fprintf(stderr, "missing value for config key: %s\n", key_str);
			continue;
		}

		if(!isdigit(val_str[0])) {
			fprintf(stderr, "invalid value (%s), for key: %s. expected a number.\n", val_str, key_str);
			continue;
		}

		if(strcmp(key_str, "dead-zone") == 0) {
			dead_threshold = atoi(val_str);
			printf("config: dead-zone = %d\n", dead_threshold);
		} else if(strcmp(key_str, "sensitivity") == 0) {
			sensitivity = atof(val_str);
			printf("config: sensitivity = %.3f\n", sensitivity);
		} else {
			fprintf(stderr, "unrecognized config option: %s\n", key_str);
		}
	}

	fclose(fp);
	return 0;
}
