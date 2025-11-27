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

#ifndef CFGFILE_H_
#define CFGFILE_H_

#include <limits.h>

#define MAX_AXES		64
#define MAX_BUTTONS		64
#define MAX_CUSTOM		64
#define MAX_KEYS_PER_BUTTON	8

enum {
	LED_OFF		= 0,
	LED_ON		= 1,
	LED_AUTO	= 2
};

enum {
	KBEMU_BACKEND_AUTO,	/* auto-detect based on display server */
	KBEMU_BACKEND_X11,	/* force X11 backend */
	KBEMU_BACKEND_UINPUT	/* force uinput backend */
};

/* button actions (XXX: must correspond to SPNAV_BNACT_* in libspnav) */
enum {
	BNACT_NONE,
	BNACT_SENS_RESET,
	BNACT_SENS_INC,
	BNACT_SENS_DEC,
	BNACT_DISABLE_ROTATION,
	BNACT_DISABLE_TRANSLATION,
	BNACT_DOMINANT_AXIS,

	MAX_BNACT
};

struct cfg {
	float sensitivity, sens_trans[3], sens_rot[3];
	int dead_threshold[MAX_AXES];
	int invert[MAX_AXES];
	int map_axis[MAX_AXES];
	int map_button[MAX_BUTTONS];
	int bnact[MAX_BUTTONS];
	unsigned long kbmap[MAX_BUTTONS][MAX_KEYS_PER_BUTTON];
	int kbmap_count[MAX_BUTTONS];
	char *kbmap_str[MAX_BUTTONS];
	int swapyz;
	int led, grab_device;
	int kbemu_backend;
	char serial_dev[PATH_MAX];
	int repeat_msec;

	char *devname[MAX_CUSTOM];	/* custom USB device name list */
	int devid[MAX_CUSTOM][2];	/* custom USB vendor/product id list */
};

void default_cfg(struct cfg *cfg);
int read_cfg(const char *fname, struct cfg *cfg);
int write_cfg(const char *fname, struct cfg *cfg);

#endif	/* CFGFILE_H_ */
