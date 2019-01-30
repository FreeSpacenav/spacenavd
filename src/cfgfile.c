/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2019 John Tsiombikas <nuclear@member.fsf.org>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include "cfgfile.h"
#include "logger.h"
#include "spnavd.h"
#include "cfg/cfgopt.h"

static int cfginit(void);

static const int def_axmap[] = {0, 2, 1, 3, 5, 4};
static const int def_axinv[] = {0, 1, 1, 0, 1, 1};

void default_cfg(struct cfg *cfg)
{
	static int init_done;

	if(!init_done) {
		cfginit();
		init_done = 1;
	}
}

int read_cfg(const char *fname, struct cfg *cfg)
{
}

int write_cfg(const char *fname, struct cfg *cfg)
{
}

static int cfginit(void)
{
	add_cfgopt_int("repeat-interval", -1);
	add_cfgopt_int("dead-zone", 2);
	add_cfgopt_int("dead-zone-translation-x", 2);
	add_cfgopt_int("dead-zone-translation-y", 2);
	add_cfgopt_int("dead-zone-translation-z", 2);
	add_cfgopt_int("dead-zone-rotation-x", 2);
	add_cfgopt_int("dead-zone-rotation-y", 2);
	add_cfgopt_int("dead-zone-rotation-z", 2);
	add_cfgopt_float("sensitivity", 1);
	add_cfgopt_float("sensitivity-translation", 1);
	add_cfgopt_float("sensitivity-translation-x", 1);
	add_cfgopt_float("sensitivity-translation-y", 1);
	add_cfgopt_float("sensitivity-translation-z", 1);
	add_cfgopt_float("sensitivity-rotation", 1);
	add_cfgopt_float("sensitivity-rotation-x", 1);
	add_cfgopt_float("sensitivity-rotation-y", 1);
	add_cfgopt_float("sensitivity-rotation-z", 1);
	add_cfgopt_str("invert-rot", "yz");
	add_cfgopt_trans("invert-trans", "yz");
}
