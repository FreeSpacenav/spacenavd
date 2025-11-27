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
	/* If config forces uinput, initialize it now */
	if(cfg.kbemu_backend == KBEMU_BACKEND_UINPUT) {
		if(backend == KBEMU_NONE) {
			logmsg(LOG_INFO, "Config forces uinput backend, initializing...\n");
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

	/* If we already have a backend, don't override */
	if(backend != KBEMU_NONE) {
		logmsg(LOG_DEBUG, "kbemu backend already initialized\n");
		return;
	}

	/* Check config file for forced backend selection */
#if defined(__linux__) && defined(HAVE_UINPUT_H)
	if(cfg.kbemu_backend == KBEMU_BACKEND_UINPUT) {
		logmsg(LOG_INFO, "Config forces uinput backend\n");
		if(kbemu_uinput_init() == 0) {
			backend = KBEMU_UINPUT;
			logmsg(LOG_INFO, "Using uinput keyboard emulation backend\n");
			return;
		}
		logmsg(LOG_WARNING, "Failed to initialize uinput, falling back to X11\n");
	}
#endif

	if(cfg.kbemu_backend == KBEMU_BACKEND_X11) {
		logmsg(LOG_INFO, "Config forces X11 backend\n");
		/* Skip auto-detection, go straight to X11 */
		goto use_x11;
	}

	/* Auto-detect: On Wayland, prefer uinput over X11 (XWayland)
	 * X11 through XWayland only works for X11 apps, not native Wayland apps.
	 * uinput works universally for all apps on Wayland.
	 */
#if defined(__linux__) && defined(HAVE_UINPUT_H)
	if(cfg.kbemu_backend == KBEMU_BACKEND_AUTO) {
		int is_wayland = 0;
		char *session_type, *wayland_display;

		/* Check multiple indicators of Wayland session */
		session_type = getenv("XDG_SESSION_TYPE");
		wayland_display = getenv("WAYLAND_DISPLAY");

		logmsg(LOG_DEBUG, "Auto-detecting display server: XDG_SESSION_TYPE=%s, WAYLAND_DISPLAY=%s\n",
			session_type ? session_type : "unset",
			wayland_display ? wayland_display : "unset");

		if((session_type && strcmp(session_type, "wayland") == 0) || wayland_display) {
			is_wayland = 1;
			logmsg(LOG_INFO, "Detected Wayland session\n");
		}

		if(is_wayland) {
			logmsg(LOG_INFO, "Trying uinput backend for Wayland compatibility\n");
			if(kbemu_uinput_init() == 0) {
				backend = KBEMU_UINPUT;
				logmsg(LOG_INFO, "Using uinput keyboard emulation backend\n");
				return;
			}
			logmsg(LOG_WARNING, "Failed to initialize uinput, falling back to X11\n");
		} else {
			logmsg(LOG_DEBUG, "No Wayland session detected, will use X11 backend\n");
		}
	}
#endif

use_x11:

	/* Use X11 backend (either on native X11 or as fallback on Wayland) */
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
