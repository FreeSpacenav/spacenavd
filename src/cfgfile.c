/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2022 John Tsiombikas <nuclear@member.fsf.org>

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
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "cfgfile.h"
#include "logger.h"
#include "spnavd.h"

/* all parsable config options... some of them might map to the same cfg field */
enum {
	CFG_REPEAT,
	CFG_DEADZONE, CFG_DEADZONE_N,
	CFG_DEADZONE_TX, CFG_DEADZONE_TY, CFG_DEADZONE_TZ,
	CFG_DEADZONE_RX, CFG_DEADZONE_RY, CFG_DEADZONE_RZ,
	CFG_SENS,
	CFG_SENS_TRANS, CFG_SENS_TX, CFG_SENS_TY, CFG_SENS_TZ,
	CFG_SENS_ROT, CFG_SENS_RX, CFG_SENS_RY, CFG_SENS_RZ,
	CFG_INVROT, CFG_INVTRANS, CFG_SWAPYZ,
	CFG_AXISMAP_N, CFG_BNMAP_N, CFG_BNACT_N, CFG_KBMAP_N,
	CFG_LED, CFG_GRAB,
	CFG_SERIAL, CFG_DEVID,

	NUM_CFG_OPTIONS
};

/* number of lines to add to the cfglines allocation, in order to allow for
 * adding any number of additional options if necessary
 */
#define NUM_EXTRA_LINES	(NUM_CFG_OPTIONS + MAX_CUSTOM + MAX_BUTTONS * 3 + MAX_AXES + 16)

static int parse_bnact(const char *s);
static int add_cfgopt(int opt, int idx, const char *fmt, ...);
static int add_cfgopt_devid(int vid, int pid);

enum {TX, TY, TZ, RX, RY, RZ};

struct cfgline {
	char *str;		/* actual line text */
	int opt;		/* CFG_* item */
	int idx;
};

static struct cfgline *cfglines;
static int num_lines;


void default_cfg(struct cfg *cfg)
{
	int i;

	memset(cfg, 0, sizeof *cfg);

	cfg->sensitivity = 1.0;
	for(i=0; i<3; i++) {
		cfg->sens_trans[i] = cfg->sens_rot[i] = 1.0;
	}

	for(i=0; i<6; i++) {
		cfg->dead_threshold[i] = 2;
	}

	cfg->led = LED_ON;
	cfg->grab_device = 1;

	for(i=0; i<6; i++) {
		cfg->map_axis[i] = i;
	}

	for(i=0; i<MAX_BUTTONS; i++) {
		cfg->map_button[i] = i;
		cfg->kbmap_str[i] = 0;
		cfg->kbmap[i] = 0;
	}

	cfg->repeat_msec = -1;

	for(i=0; i<MAX_CUSTOM; i++) {
		cfg->devname[i] = 0;
		cfg->devid[i][0] = cfg->devid[i][1] = -1;
	}
}

void unlock_cfgfile(int fd)
{
	struct flock flk;
	flk.l_type = F_UNLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	fcntl(fd, F_SETLK, &flk);
}

#define EXPECT(cond) \
	do { \
		if(!(cond)) { \
			logmsg(LOG_ERR, "%s: invalid value for %s\n", __func__, key_str); \
			continue; \
		} \
	} while(0)

int read_cfg(const char *fname, struct cfg *cfg)
{
	FILE *fp;
	int i, c, fd;
	char buf[512];
	struct flock flk;
	int num_devid = 0;
	struct cfgline *lptr;

	default_cfg(cfg);

	logmsg(LOG_INFO, "reading config file: %s\n", fname);
	if(!(fp = fopen(fname, "r"))) {
		logmsg(LOG_WARNING, "failed to open config file %s: %s. using defaults.\n", fname, strerror(errno));
		return -1;
	}
	fd = fileno(fp);

	/* acquire shared read lock */
	flk.l_type = F_RDLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	while(fcntl(fd, F_SETLKW, &flk) == -1);

	/* count newlines and populate lines array */
	num_lines = 0;
	while((c = fgetc(fp)) != -1) {
		if(c == '\n') num_lines++;
	}
	rewind(fp);
	if(!num_lines) num_lines = 1;

	/* add enough lines to be able to append any number of new options */
	free(cfglines);
	if(!(cfglines = calloc(num_lines + NUM_EXTRA_LINES, sizeof *cfglines))) {
		logmsg(LOG_WARNING, "failed to allocate config lines buffer (%d lines)\n", num_lines);
		unlock_cfgfile(fd);
		fclose(fp);
		return -1;
	}

	/* parse config file */
	num_lines = 0;
	while(fgets(buf, sizeof buf, fp)) {
		int isint, isfloat, ival, bnidx, axisidx;
		float fval;
		char *endp, *key_str, *val_str, *line = buf;

		lptr = cfglines + num_lines++;

		if((endp = strchr(buf, '\r')) || (endp = strchr(buf, '\n'))) {
			*endp = 0;
		}
		if(!(lptr->str = strdup(buf))) {
			logmsg(LOG_WARNING, "failed to allocate config line buffer, skipping line %d.\n", num_lines);
			continue;
		}

		while(*line == ' ' || *line == '\t') line++;

		if(!*line || *line == '\n' || *line == '\r' || *line == '#') {
			continue;	/* ignore comments and empty lines */
		}

		if(!(key_str = strtok(line, " =\n\t\r"))) {
			logmsg(LOG_WARNING, "invalid config line: %s, skipping.\n", line);
			continue;
		}
		if(!(val_str = strtok(0, " =\n\t\r"))) {
			logmsg(LOG_WARNING, "missing value for config key: %s\n", key_str);
			continue;
		}

		ival = strtol(val_str, &endp, 10);
		isint = (endp > val_str);

		fval = strtod(val_str, &endp);
		isfloat = (endp > val_str);

		if(strcmp(key_str, "repeat-interval") == 0) {
			lptr->opt = CFG_REPEAT;
			EXPECT(isint);
			cfg->repeat_msec = ival;

		} else if(strcmp(key_str, "dead-zone") == 0) {
			lptr->opt = CFG_DEADZONE;
			EXPECT(isint);
			for(i=0; i<MAX_AXES; i++) {
				cfg->dead_threshold[i] = ival;
			}

		} else if(sscanf(key_str, "dead-zone%d", &axisidx) == 1) {
			if(axisidx < 0 || axisidx >= MAX_AXES) {
				logmsg(LOG_WARNING, "invalid option %s, valid input axis numbers 0 - %d\n", key_str, MAX_AXES - 1);
				continue;
			}
			lptr->opt = CFG_DEADZONE_N;
			lptr->idx = axisidx;
			cfg->dead_threshold[axisidx] = ival;

		} else if(strcmp(key_str, "dead-zone-translation-x") == 0) {
			logmsg(LOG_WARNING, "Deprecated option: %s. You are encouraged to use dead-zoneN instead\n", key_str);
			lptr->opt = CFG_DEADZONE_TX;
			EXPECT(isint);
			cfg->dead_threshold[0] = ival;

		} else if(strcmp(key_str, "dead-zone-translation-y") == 0) {
			logmsg(LOG_WARNING, "Deprecated option: %s. You are encouraged to use dead-zoneN instead\n", key_str);
			lptr->opt = CFG_DEADZONE_TY;
			EXPECT(isint);
			cfg->dead_threshold[1] = ival;

		} else if(strcmp(key_str, "dead-zone-translation-z") == 0) {
			logmsg(LOG_WARNING, "Deprecated option: %s. You are encouraged to use dead-zoneN instead\n", key_str);
			lptr->opt = CFG_DEADZONE_TZ;
			EXPECT(isint);
			cfg->dead_threshold[2] = ival;

		} else if(strcmp(key_str, "dead-zone-rotation-x") == 0) {
			logmsg(LOG_WARNING, "Deprecated option: %s. You are encouraged to use dead-zoneN instead\n", key_str);
			lptr->opt = CFG_DEADZONE_RX;
			EXPECT(isint);
			cfg->dead_threshold[3] = ival;

		} else if(strcmp(key_str, "dead-zone-rotation-y") == 0) {
			logmsg(LOG_WARNING, "Deprecated option: %s. You are encouraged to use dead-zoneN instead\n", key_str);
			lptr->opt = CFG_DEADZONE_RY;
			EXPECT(isint);
			cfg->dead_threshold[4] = ival;

		} else if(strcmp(key_str, "dead-zone-rotation-z") == 0) {
			logmsg(LOG_WARNING, "Deprecated option: %s. You are encouraged to use dead-zoneN instead\n", key_str);
			lptr->opt = CFG_DEADZONE_RZ;
			EXPECT(isint);
			cfg->dead_threshold[5] = ival;

		} else if(strcmp(key_str, "sensitivity") == 0) {
			lptr->opt = CFG_SENS;
			EXPECT(isfloat);
			cfg->sensitivity = fval;

		} else if(strcmp(key_str, "sensitivity-translation") == 0) {
			lptr->opt = CFG_SENS_TRANS;
			EXPECT(isfloat);
			cfg->sens_trans[0] = cfg->sens_trans[1] = cfg->sens_trans[2] = fval;

		} else if(strcmp(key_str, "sensitivity-translation-x") == 0) {
			lptr->opt = CFG_SENS_TX;
			EXPECT(isfloat);
			cfg->sens_trans[0] = fval;

		} else if(strcmp(key_str, "sensitivity-translation-y") == 0) {
			lptr->opt = CFG_SENS_TY;
			EXPECT(isfloat);
			cfg->sens_trans[1] = fval;

		} else if(strcmp(key_str, "sensitivity-translation-z") == 0) {
			lptr->opt = CFG_SENS_TZ;
			EXPECT(isfloat);
			cfg->sens_trans[2] = fval;

		} else if(strcmp(key_str, "sensitivity-rotation") == 0) {
			lptr->opt = CFG_SENS_ROT;
			EXPECT(isfloat);
			cfg->sens_rot[0] = cfg->sens_rot[1] = cfg->sens_rot[2] = fval;

		} else if(strcmp(key_str, "sensitivity-rotation-x") == 0) {
			lptr->opt = CFG_SENS_RX;
			EXPECT(isfloat);
			cfg->sens_rot[0] = fval;

		} else if(strcmp(key_str, "sensitivity-rotation-y") == 0) {
			lptr->opt = CFG_SENS_RY;
			EXPECT(isfloat);
			cfg->sens_rot[1] = fval;

		} else if(strcmp(key_str, "sensitivity-rotation-z") == 0) {
			lptr->opt = CFG_SENS_RZ;
			EXPECT(isfloat);
			cfg->sens_rot[2] = fval;

		} else if(strcmp(key_str, "invert-rot") == 0) {
			lptr->opt = CFG_INVROT;
			if(strchr(val_str, 'x')) {
				cfg->invert[RX] = 1;
			}
			if(strchr(val_str, 'y')) {
				cfg->invert[RY] = 1;
			}
			if(strchr(val_str, 'z')) {
				cfg->invert[RZ] = 1;
			}

		} else if(strcmp(key_str, "invert-trans") == 0) {
			lptr->opt = CFG_INVTRANS;
			if(strchr(val_str, 'x')) {
				cfg->invert[TX] = 1;
			}
			if(strchr(val_str, 'y')) {
				cfg->invert[TY] = 1;
			}
			if(strchr(val_str, 'z')) {
				cfg->invert[TZ] = 1;
			}

		} else if(strcmp(key_str, "swap-yz") == 0) {
			lptr->opt = CFG_SWAPYZ;
			if(isint) {
				cfg->swapyz = ival;
			} else {
				if(strcmp(val_str, "true") == 0 || strcmp(val_str, "on") == 0 || strcmp(val_str, "yes") == 0) {
					cfg->swapyz = 1;
				} else if(strcmp(val_str, "false") == 0 || strcmp(val_str, "off") == 0 || strcmp(val_str, "no") == 0) {
					cfg->swapyz = 0;
				} else {
					logmsg(LOG_WARNING, "invalid configuration value for %s, expected a boolean value.\n", key_str);
					continue;
				}
			}

		} else if(sscanf(key_str, "axismap%d", &axisidx) == 1) {
			EXPECT(isint);
			if(axisidx < 0 || axisidx >= MAX_AXES) {
				logmsg(LOG_WARNING, "invalid option %s, valid input axis numbers 0 - %d\n", key_str, MAX_AXES - 1);
				continue;
			}
			if(ival < 0 || ival >= 6) {
				logmsg(LOG_WARNING, "invalid config value for %s, expected a number from 0 to 6\n", key_str);
				continue;
			}
			lptr->opt = CFG_AXISMAP_N;
			lptr->idx = axisidx;
			cfg->map_axis[axisidx] = ival;

		} else if(sscanf(key_str, "bnmap%d", &bnidx) == 1) {
			EXPECT(isint);
			if(bnidx < 0 || bnidx >= MAX_BUTTONS || ival < 0 || ival >= MAX_BUTTONS) {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a number from 0 to %d\n", key_str, MAX_BUTTONS);
				continue;
			}
			if(cfg->map_button[bnidx] != bnidx) {
				logmsg(LOG_WARNING, "warning: multiple mappings for button %d\n", bnidx);
			}
			lptr->opt = CFG_BNMAP_N;
			lptr->idx = bnidx;
			cfg->map_button[bnidx] = ival;

		} else if(sscanf(key_str, "bnact%d", &bnidx) == 1) {
			if(bnidx < 0 || bnidx >= MAX_BUTTONS) {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a number from 0 to %d\n", key_str, MAX_BUTTONS);
				continue;
			}
			lptr->opt = CFG_BNACT_N;
			lptr->idx = bnidx;
			if((cfg->bnact[bnidx] = parse_bnact(val_str)) == -1) {
				cfg->bnact[bnidx] = BNACT_NONE;
				logmsg(LOG_WARNING, "invalid button action: \"%s\"\n", val_str);
				continue;
			}

		} else if(sscanf(key_str, "kbmap%d", &bnidx) == 1) {
			if(bnidx < 0 || bnidx >= MAX_BUTTONS) {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a number from 0 to %d\n", key_str, MAX_BUTTONS);
				continue;
			}
			lptr->opt = CFG_KBMAP_N;
			lptr->idx = bnidx;
			if(cfg->kbmap_str[bnidx]) {
				logmsg(LOG_WARNING, "warning: multiple keyboard mappings for button %d: %s -> %s\n", bnidx, cfg->kbmap_str[bnidx], val_str);
				free(cfg->kbmap_str[bnidx]);
			}
			cfg->kbmap_str[bnidx] = strdup(val_str);

		} else if(strcmp(key_str, "led") == 0) {
			lptr->opt = CFG_LED;
			if(isint) {
				cfg->led = ival;
			} else {
				if(strcmp(val_str, "auto") == 0) {
					cfg->led = LED_AUTO;
				} else if(strcmp(val_str, "true") == 0 || strcmp(val_str, "on") == 0 || strcmp(val_str, "yes") == 0) {
					cfg->led = LED_ON;
				} else if(strcmp(val_str, "false") == 0 || strcmp(val_str, "off") == 0 || strcmp(val_str, "no") == 0) {
					cfg->led = LED_OFF;
				} else {
					logmsg(LOG_WARNING, "invalid configuration value for %s, expected a boolean value.\n", key_str);
					continue;
				}
			}

		} else if(strcmp(key_str, "grab") == 0) {
			lptr->opt = CFG_GRAB;
			if(isint) {
				cfg->grab_device = ival;
			} else {
				if(strcmp(val_str, "true") == 0 || strcmp(val_str, "on") == 0 || strcmp(val_str, "yes") == 0) {
					cfg->grab_device = 1;
				} else if(strcmp(val_str, "false") == 0 || strcmp(val_str, "off") == 0 || strcmp(val_str, "no") == 0) {
					cfg->grab_device = 0;
				} else {
					logmsg(LOG_WARNING, "invalid configuration value for %s, expected a boolean value.\n", key_str);
					continue;
				}
			}

		} else if(strcmp(key_str, "serial") == 0) {
			lptr->opt = CFG_SERIAL;
			strncpy(cfg->serial_dev, val_str, PATH_MAX - 1);

		} else if(strcmp(key_str, "device-id") == 0) {
			unsigned int vendor, prod;
			lptr->opt = CFG_DEVID;
			if(sscanf(val_str, "%x:%x", &vendor, &prod) == 2) {
				cfg->devid[num_devid][0] = (int)vendor;
				cfg->devid[num_devid][1] = (int)prod;
				num_devid++;
			} else {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a vendorid:productid pair\n", key_str);
				continue;
			}

		} else {
			logmsg(LOG_WARNING, "unrecognized config option: %s\n", key_str);
		}
	}

	unlock_cfgfile(fd);
	fclose(fp);
	return 0;
}

int write_cfg(const char *fname, struct cfg *cfg)
{
	int i, same;
	FILE *fp;
	struct flock flk;
	struct cfg def;

	if(!(fp = fopen(fname, "w"))) {
		logmsg(LOG_ERR, "failed to write config file %s: %s\n", fname, strerror(errno));
		return -1;
	}

	default_cfg(&def);	/* default config for comparisons */

	if(cfg->sensitivity != def.sensitivity) {
		add_cfgopt(CFG_SENS, 0, "sensitivity = %.3f", cfg->sensitivity);
	}

	if(cfg->sens_trans[0] == cfg->sens_trans[1] && cfg->sens_trans[1] == cfg->sens_trans[2]) {
		if(cfg->sens_trans[0] != def.sens_trans[0]) {
			add_cfgopt(CFG_SENS_TRANS, 0, "sensitivity-translation = %.3f", cfg->sens_trans[0]);
		}
	} else {
		if(cfg->sens_trans[0] != def.sens_trans[0]) {
			add_cfgopt(CFG_SENS_TX, 0, "sensitivity-translation-x = %.3f", cfg->sens_trans[0]);
		}
		if(cfg->sens_trans[1] != def.sens_trans[1]) {
			add_cfgopt(CFG_SENS_TY, 0, "sensitivity-translation-y = %.3f", cfg->sens_trans[1]);
		}
		if(cfg->sens_trans[2] != def.sens_trans[2]) {
			add_cfgopt(CFG_SENS_TZ, 0, "sensitivity-translation-z = %.3f", cfg->sens_trans[2]);
		}
	}

	if(cfg->sens_rot[0] == cfg->sens_rot[1] && cfg->sens_rot[1] == cfg->sens_rot[2]) {
		if(cfg->sens_rot[0] != def.sens_rot[0]) {
			add_cfgopt(CFG_SENS_ROT, 0, "sensitivity-rotation = %.3f", cfg->sens_rot[0]);
		}
	} else {
		if(cfg->sens_rot[0] != def.sens_rot[0]) {
			add_cfgopt(CFG_SENS_RX, 0, "sensitivity-rotation-x = %.3f", cfg->sens_rot[0]);
		}
		if(cfg->sens_rot[1] != def.sens_rot[1]) {
			add_cfgopt(CFG_SENS_RY, 0, "sensitivity-rotation-y = %.3f", cfg->sens_rot[1]);
		}
		if(cfg->sens_rot[2] != def.sens_rot[2]) {
			add_cfgopt(CFG_SENS_RZ, 0, "sensitivity-rotation-z = %.3f", cfg->sens_rot[2]);
		}
	}

	same = 1;
	for(i=1; i<MAX_AXES; i++) {
		if(cfg->dead_threshold[i] != cfg->dead_threshold[i - 1]) {
			same = 0;
			break;
		}
	}
	if(same) {
		if(cfg->dead_threshold[0] != def.dead_threshold[0]) {
			add_cfgopt(CFG_DEADZONE, 0, "dead-zone = %d", cfg->dead_threshold[0]);
		}
	} else {
		for(i=0; i<MAX_AXES; i++) {
			if(cfg->dead_threshold[i] != def.dead_threshold[i]) {
				add_cfgopt(CFG_DEADZONE_N, i, "dead-zone%d = %d", i, cfg->dead_threshold[i]);
			}
		}
	}

	if(cfg->repeat_msec != def.repeat_msec) {
		add_cfgopt(CFG_REPEAT, 0, "repeat-interval = %d\n", cfg->repeat_msec);
	}

	if(cfg->invert[0] || cfg->invert[1] || cfg->invert[2]) {
		char flags[4] = {0}, *p = flags;
		if(cfg->invert[0]) *p++ = 'x';
		if(cfg->invert[1]) *p++ = 'y';
		if(cfg->invert[2]) *p = 'z';
		add_cfgopt(CFG_INVTRANS, 0, "invert-trans = %s", flags);
	}

	if(cfg->invert[3] || cfg->invert[4] || cfg->invert[5]) {
		char flags[4] = {0}, *p = flags;
		if(cfg->invert[3]) *p++ = 'x';
		if(cfg->invert[4]) *p++ = 'y';
		if(cfg->invert[5]) *p = 'z';
		add_cfgopt(CFG_INVROT, 0, "invert-rot = %s", flags);
	}

	if(cfg->swapyz) {
		add_cfgopt(CFG_SWAPYZ, 0, "swap-yz = true");
	}

	for(i=0; i<MAX_BUTTONS; i++) {
		if(cfg->map_button[i] != i) {
			add_cfgopt(CFG_BNMAP_N, i, "bnmap%d = %d", i, cfg->map_button[i]);
		}
	}

	for(i=0; i<MAX_BUTTONS; i++) {
		if(cfg->kbmap_str[i]) {
			add_cfgopt(CFG_KBMAP_N, i, "kbmap%d = %s", i, cfg->kbmap_str[i]);
		}
	}

	if(cfg->led != def.led) {
		add_cfgopt(CFG_LED, 0, "led = %s", (cfg->led ? (cfg->led == LED_AUTO ? "auto" : "on") : "off"));
	}

	if(cfg->grab_device != def.grab_device) {
		add_cfgopt(CFG_GRAB, 0, "grab = %s", cfg->grab_device ? "true" : "false");
	}

	if(cfg->serial_dev[0]) {
		add_cfgopt(CFG_SERIAL, 0, "serial = %s", cfg->serial_dev);
	}

	for(i=0; i<MAX_CUSTOM; i++) {
		if(cfg->devid[i][0] != -1 && cfg->devid[i][1] != -1) {
			add_cfgopt_devid(cfg->devid[i][0], cfg->devid[i][1]);
		}
	}

	/* acquire exclusive write lock */
	flk.l_type = F_WRLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	while(fcntl(fileno(fp), F_SETLKW, &flk) == -1);

	for(i=0; i<num_lines; i++) {
		if(cfglines[i].str && *cfglines[i].str) {
			fputs(cfglines[i].str, fp);
		}
		fputc('\n', fp);
	}

	/* unlock */
	flk.l_type = F_UNLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	fcntl(fileno(fp), F_SETLK, &flk);

	fclose(fp);
	return 0;
}

static struct {
	const char *name;
	int act;
} bnact_strtab[] = {
	{"none", BNACT_NONE},
	{"sensitivity-up", BNACT_SENS_INC},
	{"sensitivity-down", BNACT_SENS_DEC},
	{"sensitivity-reset", BNACT_SENS_RESET},
	{"disable-rotation", BNACT_DISABLE_ROTATION},
	{"disable-translation", BNACT_DISABLE_TRANSLATION},
	{0, 0}
};

static int parse_bnact(const char *s)
{
	int i;
	for(i=0; bnact_strtab[i].name; i++) {
		if(strcmp(bnact_strtab[i].name, s) == 0) {
			return bnact_strtab[i].act;
		}
	}
	return -1;
}

static struct cfgline *find_cfgopt(int opt, int idx)
{
	int i;
	for(i=0; i<num_lines; i++) {
		if(cfglines[i].str && cfglines[i].opt == opt && cfglines[i].idx == idx) {
			return cfglines + i;
		}
	}
	return 0;
}

static int add_cfgopt(int opt, int idx, const char *fmt, ...)
{
	struct cfgline *lptr;
	char buf[512], *str;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	if(!(str = strdup(buf))) return -1;

	if(!(lptr = find_cfgopt(opt, idx))) {
		num_lines++;	/* leave an empty line */
		lptr = cfglines + num_lines++;
	}
	free(lptr->str);
	lptr->str = str;
	lptr->opt = opt;
	lptr->idx = idx;
	return 0;
}

static int add_cfgopt_devid(int vid, int pid)
{
	int i;
	unsigned int dev[2];
	struct cfgline *lptr = 0;
	char *str, *val;

	if(!(str = malloc(64))) return -1;
	sprintf(str, "device-id = %04x:%04x", vid, pid);

	for(i=0; i<num_lines; i++) {
		if(!cfglines[i].str || cfglines[i].opt != CFG_DEVID) {
			continue;
		}
		if(!(val = strchr(cfglines[i].str, '='))) {
			continue;
		}
		if(sscanf(val + 1, "%x:%x", dev, dev + 1) == 2 && dev[0] == vid && dev[1] == pid) {
			lptr = cfglines + i;
			break;
		}
	}

	if(!lptr) {
		num_lines++;	/* leave an empty line */
		lptr = cfglines + num_lines++;
	}

	free(lptr->str);
	lptr->str = str;
	lptr->opt = CFG_DEVID;
	lptr->idx = 0;
	return 0;
}
