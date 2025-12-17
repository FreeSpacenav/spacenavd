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
#define MAX_PROFILES		16

enum {
	LED_OFF		= 0,
	LED_ON		= 1,
	LED_AUTO	= 2
};

struct profile {
	char *name;		/* profile name (e.g., "blender", "cad") */
	char *appname;	/* optional application name for auto-switching */
	char **config_overrides;	/* array of "key = value" strings for inline config */
	int num_overrides;
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
	BNACT_PROFILE_0,
	BNACT_PROFILE_1,
	BNACT_PROFILE_2,
	BNACT_PROFILE_3,
	BNACT_PROFILE_4,
	BNACT_PROFILE_5,
	BNACT_PROFILE_6,
	BNACT_PROFILE_7,
	BNACT_PROFILE_8,
	BNACT_PROFILE_9,
	BNACT_PROFILE_10,
	BNACT_PROFILE_11,
	BNACT_PROFILE_12,
	BNACT_PROFILE_13,
	BNACT_PROFILE_14,
	BNACT_PROFILE_15,

	MAX_BNACT
};

struct cfg {
	float sensitivity, sens_trans[3], sens_rot[3];
	int dead_threshold[MAX_AXES];
	int invert[MAX_AXES];
	int map_axis[MAX_AXES];
	int map_button[MAX_BUTTONS];
	int bnact[MAX_BUTTONS];
	unsigned int kbmap[MAX_BUTTONS][MAX_KEYS_PER_BUTTON];
	int kbmap_count[MAX_BUTTONS];
	char *kbmap_str[MAX_BUTTONS];
	int swapyz;
	int led, grab_device;
	char serial_dev[PATH_MAX];
	int repeat_msec;

	char *devname[MAX_CUSTOM];	/* custom USB device name list */
	int devid[MAX_CUSTOM][2];	/* custom USB vendor/product id list */

	/* debug options, might change at any time */
	int kbemu_use_x11;			/* force X11 for kbemu, instead of uinput */

	/* profile support */
	struct profile profiles[MAX_PROFILES];
	int num_profiles;
};

void default_cfg(struct cfg *cfg);
int read_cfg(const char *fname, struct cfg *cfg);
int write_cfg(const char *fname, struct cfg *cfg);

/* profile management */
int switch_profile(int profile_idx);
int get_current_profile(void);
const char *get_profile_name(int profile_idx);
int find_profile_by_appname(const char *appname);
void switch_profile_by_appname(const char *appname);
void restore_previous_profile(void);

#endif	/* CFGFILE_H_ */
