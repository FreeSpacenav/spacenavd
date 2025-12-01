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

#ifdef HAVE_UINPUT_H
int kbemu_uinput_init(void);
void kbemu_uinput_cleanup(void);
#endif

#ifdef USE_X11
int kbemu_x11_init(void);
#endif

unsigned int kbemu_x11_keysym(const char *str);
const char *kbemu_x11_keyname(unsigned int sym);

static void dummy_sendkey(unsigned int key, int press);
static void dummy_sendcombo(unsigned int *keys, int count, int press);

extern struct cfg cfg;

/* For now we'll initialize these to the X11 variants statically, because we
 * need them when we read the config file. Eventually we should implement our
 * own translation functions that don't depend on X11 (TODO).
 */
unsigned int (*kbemu_keysym)(const char *str) = kbemu_x11_keysym;
const char *(*kbemu_keyname)(unsigned int sym) = kbemu_x11_keyname;

void (*kbemu_send_key)(unsigned int key, int press);
void (*kbemu_send_combo)(unsigned int *keys, int count, int press);


void kbemu_init(void)
{
	kbemu_send_key = dummy_sendkey;
	kbemu_send_combo = dummy_sendcombo;

#ifdef USE_X11
	kbemu_x11_init();
#endif

#ifdef HAVE_UINPUT_H
	if(!cfg.kbemu_use_x11) {
		kbemu_uinput_init();
	}
#endif
}

void kbemu_cleanup(void)
{
#ifdef HAVE_UINPUT_H
	kbemu_uinput_cleanup();
#endif
}

int kbemu_active(void)
{
	return kbemu_send_key && kbemu_send_key != dummy_sendkey;
}

static void dummy_sendkey(unsigned int key, int press)
{
	logmsg(LOG_DEBUG, "dummy_sendkey\n");
}

static void dummy_sendcombo(unsigned int *keys, int count, int press)
{
	logmsg(LOG_DEBUG, "dummy_sendcombo\n");
}
