/*
spacenavd - a free software replacement driver for 6dof space-mice.
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
#ifndef KEYMAP_H_
#define KEYMAP_H_

#ifdef USE_X11
#include <X11/keysym.h>
#else
/* Define minimal X11 KeySym values when X11 is not available */
typedef unsigned long KeySym;
#define XK_VoidSymbol 0xffffff
#endif

#ifdef __linux__

/* Map X11 KeySym to Linux KEY_* code
 * Note: KeySym is defined above, either from X11 or as typedef */
unsigned int keysym_to_linux_keycode(unsigned long sym);

#endif	/* __linux__ */

#endif	/* KEYMAP_H_ */
