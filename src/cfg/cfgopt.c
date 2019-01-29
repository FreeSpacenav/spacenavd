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
#include "cfgopt.h"
#include "rbtree.h"
#include "logger.h"

static void optfree(struct rbnode *n, void *cls);

static struct rbtree *opts;


void destroy_cfgopt(struct cfgopt *opt)
{
	free(opt->name);
	free(opt->sval);
}

int set_cfgopt_name(struct cfgopt *opt, const char *name)
{
	int len = strlen(name);
	char *tmp;

	if(!(tmp = malloc(len + 1))) {
		return -1;
	}
	free(opt->name);
	opt->name = tmp;
	memcpy(opt->name, name, len + 1);
	return 0;
}

int set_cfgopt_parse_value(struct cfgopt *opt, const char *val)
{
	char *endp;

	if(set_cfgopt_str_value(opt, val) == -1) {
		return -1;
	}

	opt->ival = strtol(val, &endp, 0);
	if(endp > val && *endp == 0) {
		opt->type = OPT_INT;
	}

	opt->fval = strtod(val, &endp);
	if(endp > val && *endp == 0 && opt->fval != (float)opt->ival) {
		opt->type = OPT_FLOAT;
	}
	return 0;
}

int set_cfgopt_str_value(struct cfgopt *opt, const char *val)
{
	int len = strlen(val);
	char *tmp;

	if(!(tmp = malloc(len + 1))) {
		return -1;
	}
	free(opt->sval);
	opt->sval = tmp;
	memcpy(opt->sval, val, len + 1);
	opt->type = OPT_STR;
	return 0;
}

int set_cfgopt_int_value(struct cfgopt *opt, int val)
{
	char buf[32];

	sprintf(buf, "%d", val);
	if(set_cfgopt_str_value(opt, buf) == -1) {
		return -1;
	}
	opt->ival = val;
	opt->fval = (float)val;
	opt->type = OPT_INT;
	return 0;
}

int set_cfgopt_float_value(struct cfgopt *opt, float val)
{
	char buf[64];

	sprintf(buf, "%f", val);
	if(set_cfgopt_str_value(opt, buf) == -1) {
		return -1;
	}
	opt->fval = val;
	opt->ival = (int)val;
	opt->type = OPT_FLOAT;
	return 0;
}

int add_cfgopt(struct cfgopt *opt)
{
	if(!opts) {
		if(!(opts = rb_create(RB_KEY_STRING))) {
			logmsg(LOG_ERR, "add_cfgopt: failed to create options tree\n");
			return -1;
		}
		rb_set_delete_func(opts, optfree, 0);
	}

	return rb_insert(opts, opt->name, opt);
}

int add_cfgopt_str(const char *name, const char *defval)
{
	struct cfgopt *o;

	if(!(o = calloc(1, sizeof *o))) {
		logmsg(LOG_ERR, "failed to allocate config option\n");
		return -1;
	}
	if(set_cfgopt_str_value(o, defval) == -1) {
		free(o);
		logmsg(LOG_ERR, "failed to set config option value\n");
		return -1;
	}
	return add_cfgopt(o);
}

int add_cfgopt_int(const char *name, int defval)
{
	struct cfgopt *o;

	if(!(o = calloc(1, sizeof *o))) {
		logmsg(LOG_ERR, "failed to allocate config option\n");
		return -1;
	}
	if(set_cfgopt_int_value(o, defval) == -1) {
		free(o);
		logmsg(LOG_ERR, "failed to set config option value\n");
		return -1;
	}
	return add_cfgopt(o);
}

int add_cfgopt_float(const char *name, float defval)
{
	struct cfgopt *o;

	if(!(o = calloc(1, sizeof *o))) {
		logmsg(LOG_ERR, "failed to allocate config option\n");
		return -1;
	}
	if(set_cfgopt_float_value(o, defval) == -1) {
		free(o);
		logmsg(LOG_ERR, "failed to set config option value\n");
		return -1;
	}
	return add_cfgopt(o);
}

static void optfree(struct rbnode *n, void *cls)
{
	destroy_cfgopt(rb_node_data(n));
}
