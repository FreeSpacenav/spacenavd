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
#ifndef KBEMU_H_
#define KBEMU_H_

#ifdef USE_X11
#include <X11/Xlib.h>
#include <X11/keysym.h>

void kbemu_set_display(Display *dpy);
#else
/* When X11 is not available, define KeySym for API compatibility */
typedef unsigned long KeySym;
#endif

/* Initialize keyboard emulation based on config */
void kbemu_init(void);

KeySym kbemu_keysym(const char *str);
const char *kbemu_keyname(KeySym sym);

void send_kbevent(KeySym key, int press);
void send_kbevent_combo(KeySym *keys, int count, int press);

/* Backend-specific functions */
#ifdef USE_X11
int kbemu_x11_init(Display *dpy);
void kbemu_x11_cleanup(void);
void kbemu_x11_send_key(KeySym key, int press);
void kbemu_x11_send_key_combo(KeySym *keys, int count, int press);
#endif

#if defined(__linux__) && defined(HAVE_UINPUT_H)
int kbemu_uinput_init(void);
void kbemu_uinput_cleanup(void);
void kbemu_uinput_send_key(KeySym key, int press);
void kbemu_uinput_send_key_combo(KeySym *keys, int count, int press);
#endif

#endif	/* KBEMU_H_ */
