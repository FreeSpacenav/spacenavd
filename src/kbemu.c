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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logger.h"
#include "kbemu.h"
#include "cfgfile.h"

enum {
	KBEMU_NONE,
	KBEMU_X11,
	KBEMU_UINPUT
};

static int backend = KBEMU_NONE;

extern struct cfg cfg;

/* Initialize keyboard emulation based on config, independent of X11 */
void kbemu_init(void)
{
#if defined(__linux__) && defined(HAVE_UINPUT_H)
	/* For uinput builds, default to uinput unless kbmap_use_x11 is set */
	if(!cfg.kbemu_use_x11) {
		if(backend == KBEMU_NONE) {
			logmsg(LOG_INFO, "Initializing uinput keyboard emulation backend\n");
			if(kbemu_uinput_init() == 0) {
				backend = KBEMU_UINPUT;
				logmsg(LOG_INFO, "Using uinput keyboard emulation backend\n");
			} else {
				logmsg(LOG_WARNING, "Failed to initialize uinput backend\n");
			}
		}
	}
#endif
}

#ifdef USE_X11
#include <X11/Xlib.h>

/* Called by proto_x11.c when X11 connection is established */
void kbemu_set_display(Display *dpy)
{
	if(!dpy) {
		if(backend == KBEMU_X11) {
			kbemu_x11_cleanup();
			backend = KBEMU_NONE;
		}
		return;
	}

	/* If we already have a backend (from kbemu_init), don't override */
	if(backend != KBEMU_NONE) {
		logmsg(LOG_DEBUG, "kbemu backend already initialized\n");
		return;
	}

	/* Use X11 backend as fallback if uinput wasn't initialized
	 * (either because HAVE_UINPUT_H not defined, or kbmap_use_x11 was set,
	 * or uinput initialization failed)
	 */
	if(kbemu_x11_init(dpy) == 0) {
		backend = KBEMU_X11;
		logmsg(LOG_INFO, "Using X11 keyboard emulation backend\n");
		return;
	}

	logmsg(LOG_WARNING, "Failed to initialize X11 keyboard emulation\n");
}

#else  /* !USE_X11 */

/* When X11 is not available, try uinput */
static void kbemu_init_fallback(void)
{
	if(backend != KBEMU_NONE) {
		return;	/* already initialized */
	}

#if defined(__linux__) && defined(HAVE_UINPUT_H)
	logmsg(LOG_INFO, "X11 not available, trying uinput keyboard emulation\n");
	if(kbemu_uinput_init() == 0) {
		backend = KBEMU_UINPUT;
		logmsg(LOG_INFO, "Using uinput keyboard emulation backend\n");
		return;
	}
	logmsg(LOG_WARNING, "Failed to initialize uinput keyboard emulation\n");
#else
	logmsg(LOG_WARNING, "No keyboard emulation backend available\n");
#endif
}

#endif	/* USE_X11 */

KeySym kbemu_keysym(const char *str)
{
#ifdef USE_X11
	return XStringToKeysym(str);
#else
	/* Without X11, we can't parse key names properly.
	 * This function is mainly used during config parsing. */
	logmsg(LOG_WARNING, "kbemu_keysym: X11 not available, cannot parse key name: %s\n", str);
	return 0;
#endif
}

const char *kbemu_keyname(KeySym sym)
{
#ifdef USE_X11
	return XKeysymToString(sym);
#else
	static char buf[32];
	snprintf(buf, sizeof buf, "0x%lx", sym);
	return buf;
#endif
}

void send_kbevent(KeySym key, int press)
{
#ifndef USE_X11
	/* Auto-initialize if not already done */
	if(backend == KBEMU_NONE) {
		kbemu_init_fallback();
	}
#endif

	switch(backend) {
#ifdef USE_X11
	case KBEMU_X11:
		kbemu_x11_send_key(key, press);
		break;
#endif

#if defined(__linux__) && defined(HAVE_UINPUT_H)
	case KBEMU_UINPUT:
		kbemu_uinput_send_key(key, press);
		break;
#endif

	default:
		/* No backend available or not initialized */
		break;
	}
}

void send_kbevent_combo(KeySym *keys, int count, int press)
{
#ifndef USE_X11
	/* Auto-initialize if not already done */
	if(backend == KBEMU_NONE) {
		kbemu_init_fallback();
	}
#endif

	switch(backend) {
#ifdef USE_X11
	case KBEMU_X11:
		kbemu_x11_send_key_combo(keys, count, press);
		break;
#endif

#if defined(__linux__) && defined(HAVE_UINPUT_H)
	case KBEMU_UINPUT:
		kbemu_uinput_send_key_combo(keys, count, press);
		break;
#endif

	default:
		/* No backend available or not initialized */
		break;
	}
}
