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

#ifdef USE_X11
#include <stdio.h>
#include <string.h>
#include "kbemu.h"

static Display *dpy;

void kbemu_set_display(Display *d)
{
	dpy = d;
}

KeySym kbemu_keysym(const char *str)
{
	return XStringToKeysym(str);
}

void send_kbevent(KeySym key, int press)
{
	XEvent xevent;
	Window win;
	int rev_state;

	if(!dpy) return;

	XGetInputFocus(dpy, &win, &rev_state);

	xevent.type = press ? KeyPress : KeyRelease;
	xevent.xkey.display = dpy;
	xevent.xkey.root = DefaultRootWindow(dpy);
	xevent.xkey.window = win;
	xevent.xkey.subwindow = None;
	xevent.xkey.keycode = XKeysymToKeycode(dpy, key);
	xevent.xkey.state = 0;
	xevent.xkey.time = CurrentTime;
	xevent.xkey.x = xevent.xkey.y = 1;
	xevent.xkey.x_root = xevent.xkey.y_root = 1;

	XSendEvent(dpy, win, True, press ? KeyPressMask : KeyReleaseMask, &xevent);
	XFlush(dpy);
}
#endif	/* USE_X11 */
