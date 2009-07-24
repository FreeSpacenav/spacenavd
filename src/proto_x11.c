/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2009 John Tsiombikas <nuclear@member.fsf.org>

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
#include "proto_x11.h"
#include "client.h"
#include "spnavd.h"
#include "xdetect.h"


enum cmd_msg {
	CMD_NONE,
	CMD_APP_WINDOW = 27695,	/* set client window */
	CMD_APP_SENS			/* set app sensitivity */
};


static int catch_badwin(Display *dpy, XErrorEvent *err);

static Display *dpy;
static Window win;
static Atom xa_event_motion, xa_event_bpress, xa_event_brelease, xa_event_cmd;

/* XXX This stands in for the client sensitivity. Due to the
 * bad design of the original magellan protocol, we can't know
 * which client requested the sensitivity change, so we have
 * to keep it global for all X clients.
 */
static float x11_sens = 1.0;


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

	/* if the server started from init, it probably won't have a DISPLAY env var
	 * so let's add a default one.
	 */
	if(!getenv("DISPLAY")) {
		putenv("DISPLAY=:0.0");
	}

	if(verbose) {
		printf("trying to open X11 display \"%s\"\n", getenv("DISPLAY"));
	}

	if(!(dpy = XOpenDisplay(0))) {
		fprintf(stderr, "failed to open X11 display \"%s\"\n", getenv("DISPLAY"));

		xdet_start();
		return -1;
	}
	scr_count = ScreenCount(dpy);
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	/* intern the various atoms used for communicating with the magellan clients */
	xa_event_motion = XInternAtom(dpy, "MotionEvent", False);
	xa_event_bpress = XInternAtom(dpy, "ButtonPressEvent", False);
	xa_event_brelease = XInternAtom(dpy, "ButtonReleaseEvent", False);
	xa_event_cmd = XInternAtom(dpy, "CommandEvent", False);

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
		XChangeProperty(dpy, root, xa_event_cmd, cmd_type, 32, PropModeReplace, (unsigned char*)&win, 1);
	}
	XFlush(dpy);

	xdet_stop();	/* stop X server detection if it was running */
	return 0;
}

void close_x11(void)
{
	int i, scr_count;
	struct client *cnode;

	if(!dpy) return;

	if(verbose) {
		printf("closing X11 connection to display \"%s\"\n", getenv("DISPLAY"));
	}

	/* first delete all the CommandEvent properties from all root windows */
	scr_count = ScreenCount(dpy);
	for(i=0; i<scr_count; i++) {
		Window root = RootWindow(dpy, i);
		XDeleteProperty(dpy, root, xa_event_cmd);
	}

	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	dpy = 0;

	/* also remove all x11 clients from the client list */
	cnode = first_client();
	while(cnode) {
		struct client *c = cnode;
		cnode = next_client();

		if(get_client_type(c) == CLIENT_X11) {
			remove_client(c);
		}
	}
}

int get_x11_socket(void)
{
	return dpy ? ConnectionNumber(dpy) : xdet_get_fd();
}

void send_xevent(spnav_event *ev, struct client *c)
{
	int i;
	int (*prev_xerr_handler)(Display*, XErrorEvent*);
	XEvent xevent;

	if(!dpy) return;

	/* If any of the registered clients exit without notice, we can get a
	 * BadWindow exception. Thus we must install a custom handler to avoid
	 * crashing the daemon when that happens. Also catch_badwin (see below)
	 * removes that client from the list, to avoid perpetually trying to send
	 * events to an invalid window.
	 */
	prev_xerr_handler = XSetErrorHandler(catch_badwin);

	xevent.type = ClientMessage;
	xevent.xclient.send_event = False;
	xevent.xclient.display = dpy;
	xevent.xclient.window = get_client_window(c);

	switch(ev->type) {
	case EVENT_MOTION:
		xevent.xclient.message_type = xa_event_motion;
		xevent.xclient.format = 16;

		for(i=0; i<6; i++) {
			float val = (float)ev->motion.data[i] * x11_sens;
			xevent.xclient.data.s[i + 2] = (short)val;
		}
		xevent.xclient.data.s[0] = xevent.xclient.data.s[1] = 0;
		xevent.xclient.data.s[8] = ev->motion.period;
		break;

	case EVENT_BUTTON:
		xevent.xclient.message_type = ev->button.press ? xa_event_bpress : xa_event_brelease;
		xevent.xclient.format = 16;
		xevent.xclient.data.s[2] = ev->button.bnum;
		break;

	default:
		break;
	}

	XSendEvent(dpy, get_client_window(c), False, 0, &xevent);

	/* we *must* sync at this point, otherwise, a potential error may arrive
	 * after we remove the error handler and crash the daemon.
	 */
	XSync(dpy, False);
	XSetErrorHandler(prev_xerr_handler);
}

int handle_xevents(fd_set *rset)
{
	if(!dpy) {
		if(xdet_get_fd() != -1) {
			handle_xdet_events(rset);
		}
		return -1;
	}

	/* process any pending X events */
	if(FD_ISSET(ConnectionNumber(dpy), rset)) {
		while(XPending(dpy)) {
			XEvent xev;
			XNextEvent(dpy, &xev);

			if(xev.type == ClientMessage && xev.xclient.message_type == xa_event_cmd) {
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

	/* make sure we don't already have that client */
	cnode = first_client();
	while(cnode) {
		if(get_client_window(cnode) == win) {
			return;
		}
		cnode = next_client();
	}

	add_client(CLIENT_X11, &win);
}

void remove_client_window(Window win)
{
	struct client *c, *cnode;

	cnode = first_client();
	while(cnode) {
		c = cnode;
		cnode = next_client();

		if(get_client_window(c) == win) {
			remove_client(c);
			return;
		}
	}
}


/* X11 error handler for bad-windows */
static int catch_badwin(Display *dpy, XErrorEvent *err)
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

