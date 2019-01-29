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
#ifndef CFGOPT_H_
#define CFGOPT_H_

enum {
	OPT_STR,
	OPT_INT,
	OPT_FLOAT,
};

struct cfgopt {
	int type;
	char *name;
	char *sval;
	int ival;
	float fval;
};

struct cfgline {
	int lineno;
	char *line;
	struct cfgopt *opt;
	int dirty;
};

void destroy_cfgopt(struct cfgopt *opt);
int set_cfgopt_name(struct cfgopt *opt, const char *name);
int set_cfgopt_parse_value(struct cfgopt *opt, const char *val);
int set_cfgopt_str_value(struct cfgopt *opt, const char *val);
int set_cfgopt_int_value(struct cfgopt *opt, int val);
int set_cfgopt_float_value(struct cfgopt *opt, float val);

int add_cfgopt(struct cfgopt *opt);
int add_cfgopt_str(const char *name, const char *defval);
int add_cfgopt_int(const char *name, int defval);
int add_cfgopt_float(const char *name, float defval);

#endif	/* CFGOPT_H_ */
