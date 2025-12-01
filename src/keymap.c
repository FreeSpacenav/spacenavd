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
#include "config.h"

#ifdef __linux__
#include <stdio.h>
#include <linux/input-event-codes.h>

struct keymap_entry {
	unsigned int xkey;
	unsigned int linkey;
};

static struct keymap_entry keymap[] = {
	/* Alphanumeric keys */
	{0x0061, KEY_A},			/* XK_a */
	{0x0062, KEY_B},			/* XK_b */
	{0x0063, KEY_C},			/* XK_c */
	{0x0064, KEY_D},			/* XK_d */
	{0x0065, KEY_E},			/* XK_e */
	{0x0066, KEY_F},			/* XK_f */
	{0x0067, KEY_G},			/* XK_g */
	{0x0068, KEY_H},			/* XK_h */
	{0x0069, KEY_I},			/* XK_i */
	{0x006a, KEY_J},			/* XK_j */
	{0x006b, KEY_K},			/* XK_k */
	{0x006c, KEY_L},			/* XK_l */
	{0x006d, KEY_M},			/* XK_m */
	{0x006e, KEY_N},			/* XK_n */
	{0x006f, KEY_O},			/* XK_o */
	{0x0070, KEY_P},			/* XK_p */
	{0x0071, KEY_Q},			/* XK_q */
	{0x0072, KEY_R},			/* XK_r */
	{0x0073, KEY_S},			/* XK_s */
	{0x0074, KEY_T},			/* XK_t */
	{0x0075, KEY_U},			/* XK_u */
	{0x0076, KEY_V},			/* XK_v */
	{0x0077, KEY_W},			/* XK_w */
	{0x0078, KEY_X},			/* XK_x */
	{0x0079, KEY_Y},			/* XK_y */
	{0x007a, KEY_Z},			/* XK_z */

	/* Number keys */
	{0x0030, KEY_0},			/* XK_0 */
	{0x0031, KEY_1},			/* XK_1 */
	{0x0032, KEY_2},			/* XK_2 */
	{0x0033, KEY_3},			/* XK_3 */
	{0x0034, KEY_4},			/* XK_4 */
	{0x0035, KEY_5},			/* XK_5 */
	{0x0036, KEY_6},			/* XK_6 */
	{0x0037, KEY_7},			/* XK_7 */
	{0x0038, KEY_8},			/* XK_8 */
	{0x0039, KEY_9},			/* XK_9 */

	/* Function keys */
	{0xffbe, KEY_F1},			/* XK_F1 */
	{0xffbf, KEY_F2},			/* XK_F2 */
	{0xffc0, KEY_F3},			/* XK_F3 */
	{0xffc1, KEY_F4},			/* XK_F4 */
	{0xffc2, KEY_F5},			/* XK_F5 */
	{0xffc3, KEY_F6},			/* XK_F6 */
	{0xffc4, KEY_F7},			/* XK_F7 */
	{0xffc5, KEY_F8},			/* XK_F8 */
	{0xffc6, KEY_F9},			/* XK_F9 */
	{0xffc7, KEY_F10},			/* XK_F10 */
	{0xffc8, KEY_F11},			/* XK_F11 */
	{0xffc9, KEY_F12},			/* XK_F12 */

	/* Modifier keys */
	{0xffe1, KEY_LEFTSHIFT},	/* XK_Shift_L */
	{0xffe2, KEY_RIGHTSHIFT},	/* XK_Shift_R */
	{0xffe3, KEY_LEFTCTRL},		/* XK_Control_L */
	{0xffe4, KEY_RIGHTCTRL},	/* XK_Control_R */
	{0xffe5, KEY_CAPSLOCK},		/* XK_Caps_Lock */
	{0xffe7, KEY_LEFTMETA},		/* XK_Meta_L */
	{0xffe8, KEY_RIGHTMETA},	/* XK_Meta_R */
	{0xffe9, KEY_LEFTALT},		/* XK_Alt_L */
	{0xffea, KEY_RIGHTALT},		/* XK_Alt_R */
	{0xffeb, KEY_LEFTMETA},		/* XK_Super_L */
	{0xffec, KEY_RIGHTMETA},	/* XK_Super_R */

	/* Special keys */
	{0xff1b, KEY_ESC},			/* XK_Escape */
	{0xff08, KEY_BACKSPACE},	/* XK_BackSpace */
	{0xff09, KEY_TAB},			/* XK_Tab */
	{0xff0d, KEY_ENTER},		/* XK_Return */
	{0x0020, KEY_SPACE},		/* XK_space */
	{0xffff, KEY_DELETE},		/* XK_Delete */
	{0xff63, KEY_INSERT},		/* XK_Insert */
	{0xff50, KEY_HOME},			/* XK_Home */
	{0xff57, KEY_END},			/* XK_End */
	{0xff55, KEY_PAGEUP},		/* XK_Page_Up */
	{0xff56, KEY_PAGEDOWN},		/* XK_Page_Down */

	/* Arrow keys */
	{0xff51, KEY_LEFT},			/* XK_Left */
	{0xff52, KEY_UP},			/* XK_Up */
	{0xff53, KEY_RIGHT},		/* XK_Right */
	{0xff54, KEY_DOWN},			/* XK_Down */

	/* Punctuation and symbols */
	{0x0027, KEY_APOSTROPHE},	/* XK_apostrophe ' */
	{0x002c, KEY_COMMA},		/* XK_comma , */
	{0x002d, KEY_MINUS},		/* XK_minus - */
	{0x002e, KEY_DOT},			/* XK_period . */
	{0x002f, KEY_SLASH},		/* XK_slash / */
	{0x003b, KEY_SEMICOLON},	/* XK_semicolon ; */
	{0x003d, KEY_EQUAL},		/* XK_equal = */
	{0x005b, KEY_LEFTBRACE},	/* XK_bracketleft [ */
	{0x005c, KEY_BACKSLASH},	/* XK_backslash \ */
	{0x005d, KEY_RIGHTBRACE},	/* XK_bracketright ] */
	{0x0060, KEY_GRAVE},		/* XK_grave ` */

	{0, 0}	/* terminator */
};

unsigned int keysym_to_linux_keycode(unsigned int sym)
{
	int i;

	for(i = 0; keymap[i].xkey; i++) {
		if(keymap[i].xkey == sym) {
			return keymap[i].linkey;
		}
	}

	return 0;	/* not found */
}

#endif	/* __linux__ */
