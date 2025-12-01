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

void kbemu_set_display(Display *dpy);
#endif

void kbemu_init(void);
void kbemu_cleanup(void);

int kbemu_active(void);

extern unsigned int (*kbemu_keysym)(const char *str);
extern const char *(*kbemu_keyname)(unsigned int sym);

extern void (*kbemu_send_key)(unsigned int key, int press);
extern void (*kbemu_send_combo)(unsigned int *keys, int count, int press);

#endif	/* KBEMU_H_ */
