/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2025 John Tsiombikas <nuclear@mutantstargoat.com>
Copyright (C) 2025 Allin Demopolis <allindemopolis@gmail.com>

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

/* Allin Demopolis: implemented key combos */

#include "config.h"

#ifdef USE_X11
#include <stdio.h>
#include <string.h>
#include "logger.h"
#include "kbemu.h"

#ifdef HAVE_XTEST_H
#include <X11/extensions/XTest.h>
static int use_xtest;
#endif

static Display *dpy;

unsigned int kbemu_x11_keysym(const char *str);
const char *kbemu_x11_keyname(unsigned int sym);

static void send_kbevent(unsigned int key, int press);
static void send_kbcombo(unsigned int *keys, int count, int press);


int kbemu_x11_init(void)
{
	kbemu_keysym = kbemu_x11_keysym;
	kbemu_keyname = kbemu_x11_keyname;

	kbemu_send_key = send_kbevent;
	kbemu_send_combo = send_kbcombo;
	return 0;
}

void kbemu_set_display(Display *d)
{
	if(!d) return;

	/* if kbemu is already active, don't override */
	if(kbemu_active()) {
		return;
	}

	dpy = d;

#ifdef HAVE_XTEST_H
	{
		int tmp;
		use_xtest = XTestQueryExtension(dpy, &tmp, &tmp, &tmp, &tmp);
	}

	if(use_xtest)
		logmsg(LOG_DEBUG, "Using XTEST to send key events\n");
	else
#endif
		logmsg(LOG_DEBUG, "Using XSendEvent to send key events\n");

	kbemu_x11_init();	/* re-register the calls just in case */
}

unsigned int kbemu_x11_keysym(const char *str)
{
	return XStringToKeysym(str);
}

const char *kbemu_x11_keyname(unsigned int sym)
{
	return XKeysymToString(sym);
}

static void send_kbevent(unsigned int key, int press)
{
	XEvent xevent;
	Window win;
	int rev_state;
	KeyCode kc;

	if(!dpy) return;

	if(!(kc = XKeysymToKeycode(dpy, key))) {
		logmsg(LOG_WARNING, "failed to convert keysym %lu to keycode\n", key);
		return;
	}

#ifdef HAVE_XTEST_H
	if(use_xtest) {
		XTestFakeKeyEvent(dpy, kc, press, 0);
		XFlush(dpy);
		return;
	}
#endif

	XGetInputFocus(dpy, &win, &rev_state);

	xevent.type = press ? KeyPress : KeyRelease;
	xevent.xkey.display = dpy;
	xevent.xkey.root = DefaultRootWindow(dpy);
	xevent.xkey.window = win;
	xevent.xkey.subwindow = None;
	xevent.xkey.keycode = kc;
	xevent.xkey.state = 0;
	xevent.xkey.time = CurrentTime;
	xevent.xkey.x = xevent.xkey.y = 1;
	xevent.xkey.x_root = xevent.xkey.y_root = 1;

	XSendEvent(dpy, win, True, press ? KeyPressMask : KeyReleaseMask, &xevent);
	XFlush(dpy);
}

static void send_kbcombo(unsigned int *keys, int count, int press)
{
	int i;

	if(!dpy || count <= 0) return;

	if(press) {
		return;
	}

	/* send press events for all keys */
	for(i=0; i<count; i++) {
		send_kbevent(keys[i], 1);
	}

	/* send release events in reverse order */
	for(i=0; i<count; i++) {
		send_kbevent(keys[count - 1 - i], 0);
	}
}

#else
unsigned int kbemu_x11_keysym(const char *str)
{
	logmsg(LOG_WARNING, "Unable to parse key mapping \"%s\", not compiled with X11 support\n",
			str);
	return 0;
}

const char *kbemu_x11_keyname(unsigned int sym)
{
	return 0;
}
#endif	/* USE_X11 */
