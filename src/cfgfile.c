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
#include "kbemu.h"

struct cfg cfg, prev_cfg;

/* profile management */
struct cfg default_cfg_backup;	/* pristine copy of default config */
int current_profile = -1;		/* -1 = default, 0-15 = profile index */

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

	/* debug options, not part of the protocol, can change at any time */
	CFG_KBMAP_USE_X11,
	CFG_PROFILE,

	NUM_CFG_OPTIONS
};

enum { RMCFG_ALL, RMCFG_OWN };

/* number of lines to add to the cfglines allocation, in order to allow for
 * adding any number of additional options if necessary
 */
#define NUM_EXTRA_LINES	(NUM_CFG_OPTIONS + MAX_CUSTOM + MAX_BUTTONS * 3 + MAX_AXES + 16)

static int parse_bnact(const char *s, struct cfg *cfg);
static const char *bnact_name(int bnact);
static int parse_kbmap(const char *str, unsigned int *kbmap, int max_keys);
static int add_cfgopt(int opt, int idx, const char *fmt, ...);
static int add_cfgopt_devid(int vid, int pid);
static int rm_cfgopt(const char *name, int mode);

enum {TX, TY, TZ, RX, RY, RZ};

struct cfgline {
	char *str;		/* actual line text */
	int opt;		/* CFG_* item */
	int idx;
	int own;		/* added and owned by spacenavd, not in the original user config */
};

static struct cfgline *cfglines;
static int num_lines;


void default_cfg(struct cfg *cfg)
{
	int i, j;

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
	cfg->kbemu_use_x11 = 0;  /* default to uinput when available */

	for(i=0; i<6; i++) {
		cfg->map_axis[i] = i;
	}

	for(i=0; i<MAX_BUTTONS; i++) {
		cfg->map_button[i] = i;
		cfg->kbmap_str[i] = 0;
		cfg->kbmap_count[i] = 0;
		for(j=0; j<MAX_KEYS_PER_BUTTON; j++) {
			cfg->kbmap[i][j] = 0;
		}
	}

	cfg->repeat_msec = -1;

	for(i=0; i<MAX_CUSTOM; i++) {
		cfg->devname[i] = 0;
		cfg->devid[i][0] = cfg->devid[i][1] = -1;
	}

	/* initialize profiles */
	cfg->num_profiles = 0;
	for(i=0; i<MAX_PROFILES; i++) {
		cfg->profiles[i].name = 0;
		cfg->profiles[i].appname = 0;
		cfg->profiles[i].config_overrides = 0;
		cfg->profiles[i].num_overrides = 0;
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
			logmsg(LOG_ERR, "read_cfg: invalid value for %s\n", key_str); \
			continue; \
		} \
	} while(0)

/* maintain false->true order of the strings so that bit 0 of the index can be
 * used directly as the boolean value
 */
static const char *bool_str[] = {
	"false", "true", "no", "yes", "off", "on",
	0
};

static void free_profile_memory(struct cfg *cfg)
{
	int i, j;
	for(i=0; i<MAX_PROFILES; i++) {
		free(cfg->profiles[i].name);
		free(cfg->profiles[i].appname);
		if(cfg->profiles[i].config_overrides) {
			for(j=0; j<cfg->profiles[i].num_overrides; j++) {
				free(cfg->profiles[i].config_overrides[j]);
			}
			free(cfg->profiles[i].config_overrides);
		}
		cfg->profiles[i].name = 0;
		cfg->profiles[i].appname = 0;
		cfg->profiles[i].config_overrides = 0;
		cfg->profiles[i].num_overrides = 0;
	}
	cfg->num_profiles = 0;
}

/* Add a config override line to a profile */
static int add_profile_override(struct profile *profile, const char *config_line)
{
	char **new_overrides;
	char *override_copy;

	if(!(override_copy = strdup(config_line))) {
		return -1;
	}

	new_overrides = realloc(profile->config_overrides,
		(profile->num_overrides + 1) * sizeof(char*));
	if(!new_overrides) {
		free(override_copy);
		return -1;
	}

	profile->config_overrides = new_overrides;
	profile->config_overrides[profile->num_overrides] = override_copy;
	profile->num_overrides++;

	return 0;
}

static int read_cfg_internal(const char *fname, struct cfg *cfg, int reset_defaults)
{
	FILE *fp;
	int i, c, fd;
	char buf[512];
	struct flock flk;
	int num_devid = 0;
	struct cfgline *lptr;

	if(reset_defaults) {
		/* free any previously allocated profiles before resetting config */
		free_profile_memory(cfg);
		default_cfg(cfg);
	}

	logmsg(LOG_INFO, "reading config file: %s%s\n", fname,
		reset_defaults ? "" : " (overlay mode)");
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
		int isint, isfloat, isbool, ival, bnidx, axisidx;
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

		/* Check for profile-prefixed config (e.g., "blender.sensitivity = 1.5") */
		{
			char *dot = strchr(key_str, '.');
			if(dot) {
				char *profile_name = key_str;
				char *option_name = dot + 1;
				int profile_idx;
				char override_line[512];
				int found = 0;

				*dot = '\0';  /* split the string at the dot */

				/* Find the profile by name */
				for(profile_idx = 0; profile_idx < cfg->num_profiles; profile_idx++) {
					if(cfg->profiles[profile_idx].name &&
					   strcmp(cfg->profiles[profile_idx].name, profile_name) == 0) {
						/* Reconstruct the config line without the profile prefix */
						char *rest_of_line = strtok(0, "\n\r");
						if(rest_of_line) {
							snprintf(override_line, sizeof(override_line), "%s = %s %s",
								option_name, val_str, rest_of_line);
						} else {
							snprintf(override_line, sizeof(override_line), "%s = %s",
								option_name, val_str);
						}

						if(add_profile_override(&cfg->profiles[profile_idx], override_line) == 0) {
							logmsg(LOG_INFO, "  profile '%s': %s\n", profile_name, override_line);
						} else {
							logmsg(LOG_WARNING, "failed to add profile override for %s\n", profile_name);
						}

						found = 1;
						break;
					}
				}

				if(!found) {
					/* Profile not found */
					logmsg(LOG_WARNING, "profile-prefixed config for unknown profile: %s\n", profile_name);
				}
				continue;  /* skip to next line in main loop */
			}
		}

		ival = strtol(val_str, &endp, 10);
		isint = (endp > val_str);

		fval = strtod(val_str, &endp);
		isfloat = (endp > val_str);

		isbool = 0;
		if(!isint && !isfloat) {
			for(i=0; bool_str[i]; i++) {
				if(strcmp(val_str, bool_str[i]) == 0) {
					isbool = 1;
					ival = i & 1;	/* order is false, true, off, on ... */
				}
			}
		}

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
			if(isint || isbool) {
				cfg->swapyz = ival;
			} else {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a boolean value.\n", key_str);
				continue;
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
			if((cfg->bnact[bnidx] = parse_bnact(val_str, cfg)) == -1) {
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
			cfg->kbmap_count[bnidx] = parse_kbmap(val_str, cfg->kbmap[bnidx], MAX_KEYS_PER_BUTTON);

		} else if(strcmp(key_str, "led") == 0) {
			lptr->opt = CFG_LED;
			if(isint || isbool) {
				cfg->led = ival;
			} else {
				if(strcmp(val_str, "auto") == 0) {
					cfg->led = LED_AUTO;
				} else {
					logmsg(LOG_WARNING, "invalid configuration value for %s, expected a boolean value or \"auto\".\n", key_str);
					continue;
				}
			}

		} else if(strcmp(key_str, "kbmap_use_x11") == 0) {
			lptr->opt = CFG_KBMAP_USE_X11;
			if(isint || isbool) {
				cfg->kbemu_use_x11 = ival;
			} else {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a boolean value.\n", key_str);
				continue;
			}

		} else if(strcmp(key_str, "grab") == 0) {
			lptr->opt = CFG_GRAB;
			if(isint || isbool) {
				cfg->grab_device = ival;
			} else {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a boolean value.\n", key_str);
				continue;
			}

		} else if(strcmp(key_str, "serial") == 0) {
			lptr->opt = CFG_SERIAL;
			strncpy(cfg->serial_dev, val_str, PATH_MAX - 1);

		} else if(strcmp(key_str, "profile") == 0) {
			char *profile_name, *profile_appname = 0;
			char *next_token;

			lptr->opt = CFG_PROFILE;
			profile_name = val_str;

			/* check for optional app=<appname> parameter */
			next_token = strtok(0, " =\n\t\r");
			if(next_token) {
				if(strncmp(next_token, "app", 3) == 0) {
					/* got "app", next token should be the appname */
					profile_appname = strtok(0, " =\n\t\r");
				} else {
					/* assume it's the appname directly */
					profile_appname = next_token;
				}
			}

			if(cfg->num_profiles >= MAX_PROFILES) {
				logmsg(LOG_WARNING, "too many profiles defined (max %d)\n", MAX_PROFILES);
				continue;
			}

			/* allocate and store profile name and optional appname */
			cfg->profiles[cfg->num_profiles].name = strdup(profile_name);
			cfg->profiles[cfg->num_profiles].appname = profile_appname ? strdup(profile_appname) : 0;

			if(!cfg->profiles[cfg->num_profiles].name) {
				logmsg(LOG_WARNING, "failed to allocate memory for profile %d\n", cfg->num_profiles);
				free(cfg->profiles[cfg->num_profiles].name);
				free(cfg->profiles[cfg->num_profiles].appname);
				cfg->profiles[cfg->num_profiles].name = 0;
				cfg->profiles[cfg->num_profiles].appname = 0;
				continue;
			}

			if(profile_appname) {
				logmsg(LOG_INFO, "registered profile %d: %s (app: %s)\n", cfg->num_profiles,
					cfg->profiles[cfg->num_profiles].name,
					cfg->profiles[cfg->num_profiles].appname);
			} else {
				logmsg(LOG_INFO, "registered profile %d: %s\n", cfg->num_profiles,
					cfg->profiles[cfg->num_profiles].name);
			}
			cfg->num_profiles++;

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

/* read config with reset to defaults (normal mode) */
int read_cfg(const char *fname, struct cfg *cfg)
{
	return read_cfg_internal(fname, cfg, 1);
}

/* Apply profile override config lines to a cfg structure */
static void apply_profile_overrides(struct cfg *cfg, struct profile *profile)
{
	int i, j;

	if(!profile || !profile->config_overrides) {
		return;
	}

	logmsg(LOG_INFO, "Applying %d config overrides for profile '%s'\n",
		profile->num_overrides, profile->name);

	for(i = 0; i < profile->num_overrides; i++) {
		char buf[512];
		char *line, *key_str, *val_str, *endp;
		int isint, isfloat, isbool, ival, axisidx, bnidx;
		float fval;

		strncpy(buf, profile->config_overrides[i], sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = 0;
		line = buf;

		/* Skip leading whitespace */
		while(*line == ' ' || *line == '\t') line++;

		/* Skip empty lines and comments */
		if(!*line || *line == '\n' || *line == '\r' || *line == '#') {
			continue;
		}

		/* Parse key = value */
		if(!(key_str = strtok(line, " =\n\t\r"))) {
			logmsg(LOG_WARNING, "  invalid override line: %s\n", profile->config_overrides[i]);
			continue;
		}
		if(!(val_str = strtok(0, " =\n\t\r"))) {
			logmsg(LOG_WARNING, "  missing value for key: %s\n", key_str);
			continue;
		}

		/* Parse value type */
		ival = strtol(val_str, &endp, 10);
		isint = (endp > val_str);

		fval = strtod(val_str, &endp);
		isfloat = (endp > val_str);

		isbool = 0;
		if(!isint && !isfloat) {
			for(j=0; bool_str[j]; j++) {
				if(strcmp(val_str, bool_str[j]) == 0) {
					isbool = 1;
					ival = j & 1;
				}
			}
		}

		/* Apply the setting (simplified version of main parser) */
		if(strcmp(key_str, "sensitivity") == 0 && isfloat) {
			cfg->sensitivity = fval;
			logmsg(LOG_INFO, "  %s = %.3f\n", key_str, fval);
		} else if(strcmp(key_str, "sensitivity-translation") == 0 && isfloat) {
			cfg->sens_trans[0] = cfg->sens_trans[1] = cfg->sens_trans[2] = fval;
			logmsg(LOG_INFO, "  %s = %.3f\n", key_str, fval);
		} else if(strcmp(key_str, "sensitivity-rotation") == 0 && isfloat) {
			cfg->sens_rot[0] = cfg->sens_rot[1] = cfg->sens_rot[2] = fval;
			logmsg(LOG_INFO, "  %s = %.3f\n", key_str, fval);
		} else if(strcmp(key_str, "dead-zone") == 0 && isint) {
			for(j=0; j<MAX_AXES; j++) {
				cfg->dead_threshold[j] = ival;
			}
			logmsg(LOG_INFO, "  %s = %d\n", key_str, ival);
		} else if(sscanf(key_str, "dead-zone%d", &axisidx) == 1 && isint) {
			if(axisidx >= 0 && axisidx < MAX_AXES) {
				cfg->dead_threshold[axisidx] = ival;
				logmsg(LOG_INFO, "  %s = %d\n", key_str, ival);
			}
		} else if(sscanf(key_str, "bnact%d", &bnidx) == 1) {
			if(bnidx >= 0 && bnidx < MAX_BUTTONS) {
				if((cfg->bnact[bnidx] = parse_bnact(val_str, cfg)) != -1) {
					logmsg(LOG_INFO, "  %s = %s\n", key_str, val_str);
				} else {
					cfg->bnact[bnidx] = BNACT_NONE;
					logmsg(LOG_WARNING, "  invalid button action: %s\n", val_str);
				}
			}
		} else if(sscanf(key_str, "kbmap%d", &bnidx) == 1) {
			if(bnidx >= 0 && bnidx < MAX_BUTTONS) {
				if(cfg->kbmap_str[bnidx]) {
					free(cfg->kbmap_str[bnidx]);
				}
				cfg->kbmap_str[bnidx] = strdup(val_str);
				cfg->kbmap_count[bnidx] = parse_kbmap(val_str, cfg->kbmap[bnidx], MAX_KEYS_PER_BUTTON);
				logmsg(LOG_INFO, "  %s = %s\n", key_str, val_str);
			}
		} else if(strcmp(key_str, "led") == 0) {
			if(isint || isbool) {
				cfg->led = ival;
				logmsg(LOG_INFO, "  %s = %s\n", key_str, ival ? "on" : "off");
			} else if(strcmp(val_str, "auto") == 0) {
				cfg->led = LED_AUTO;
				logmsg(LOG_INFO, "  %s = auto\n", key_str);
			}
		} else if(strcmp(key_str, "grab") == 0 && (isint || isbool)) {
			cfg->grab_device = ival;
			logmsg(LOG_INFO, "  %s = %s\n", key_str, ival ? "true" : "false");
		} else {
			logmsg(LOG_INFO, "  %s = %s (applied)\n", key_str, val_str);
		}
	}
}


#ifndef HAVE_VSNPRINTF
static int vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap)
{
	return vsprintf(buf, fmt, ap);
}
#endif

int write_cfg(const char *fname, struct cfg *cfg)
{
	int i, same;
	FILE *fp;
	struct flock flk;
	struct cfg def;
	char buf[128];

	if(!(fp = fopen(fname, "w"))) {
		logmsg(LOG_ERR, "failed to write config file %s: %s\n", fname, strerror(errno));
		return -1;
	}

	if(!cfglines) {
		if(!(cfglines = calloc(NUM_EXTRA_LINES, sizeof *cfglines))) {
			logmsg(LOG_WARNING, "failed to allocate config lines buffer\n");
			fclose(fp);
			return -1;
		}
	}

	default_cfg(&def);	/* default config for comparisons */

	if(cfg->sensitivity != def.sensitivity) {
		add_cfgopt(CFG_SENS, 0, "sensitivity = %.3f", cfg->sensitivity);
	}

	if(cfg->sens_trans[0] == cfg->sens_trans[1] && cfg->sens_trans[1] == cfg->sens_trans[2]) {
		rm_cfgopt("sensitivity-translation-x", RMCFG_ALL);
		rm_cfgopt("sensitivity-translation-y", RMCFG_ALL);
		rm_cfgopt("sensitivity-translation-z", RMCFG_ALL);
		if(cfg->sens_trans[0] != def.sens_trans[0]) {
			add_cfgopt(CFG_SENS_TRANS, 0, "sensitivity-translation = %.3f", cfg->sens_trans[0]);
		} else {
			rm_cfgopt("sensitivity-translation", RMCFG_OWN);
		}
	} else {
		if(cfg->sens_trans[0] != def.sens_trans[0]) {
			add_cfgopt(CFG_SENS_TX, 0, "sensitivity-translation-x = %.3f", cfg->sens_trans[0]);
			rm_cfgopt("sensitivity-translation", RMCFG_ALL);
		} else {
			rm_cfgopt("sensitivity-translation-x", RMCFG_OWN);
		}
		if(cfg->sens_trans[1] != def.sens_trans[1]) {
			add_cfgopt(CFG_SENS_TY, 0, "sensitivity-translation-y = %.3f", cfg->sens_trans[1]);
			rm_cfgopt("sensitivity-translation", RMCFG_ALL);
		} else {
			rm_cfgopt("sensitivity-translation-y", RMCFG_OWN);
		}
		if(cfg->sens_trans[2] != def.sens_trans[2]) {
			add_cfgopt(CFG_SENS_TZ, 0, "sensitivity-translation-z = %.3f", cfg->sens_trans[2]);
			rm_cfgopt("sensitivity-translation", RMCFG_ALL);
		} else {
			rm_cfgopt("sensitivity-translation-z", RMCFG_OWN);
		}
	}

	if(cfg->sens_rot[0] == cfg->sens_rot[1] && cfg->sens_rot[1] == cfg->sens_rot[2]) {
		rm_cfgopt("sensitivity-rotation-x", RMCFG_ALL);
		rm_cfgopt("sensitivity-rotation-y", RMCFG_ALL);
		rm_cfgopt("sensitivity-rotation-z", RMCFG_ALL);
		if(cfg->sens_rot[0] != def.sens_rot[0]) {
			add_cfgopt(CFG_SENS_ROT, 0, "sensitivity-rotation = %.3f", cfg->sens_rot[0]);
		} else {
			rm_cfgopt("sensitivity-rotation", RMCFG_OWN);
		}
	} else {
		if(cfg->sens_rot[0] != def.sens_rot[0]) {
			add_cfgopt(CFG_SENS_RX, 0, "sensitivity-rotation-x = %.3f", cfg->sens_rot[0]);
			rm_cfgopt("sensitivity-rotation", RMCFG_ALL);
		} else {
			rm_cfgopt("sensitivity-rotation-x", RMCFG_OWN);
		}
		if(cfg->sens_rot[1] != def.sens_rot[1]) {
			add_cfgopt(CFG_SENS_RY, 0, "sensitivity-rotation-y = %.3f", cfg->sens_rot[1]);
			rm_cfgopt("sensitivity-rotation", RMCFG_ALL);
		} else {
			rm_cfgopt("sensitivity-rotation-y", RMCFG_OWN);
		}
		if(cfg->sens_rot[2] != def.sens_rot[2]) {
			add_cfgopt(CFG_SENS_RZ, 0, "sensitivity-rotation-z = %.3f", cfg->sens_rot[2]);
			rm_cfgopt("sensitivity-rotation", RMCFG_ALL);
		} else {
			rm_cfgopt("sensitivity-rotation-z", RMCFG_OWN);
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
			for(i=0; i<MAX_AXES; i++) {
				sprintf(buf, "dead-zone%d", i);
				rm_cfgopt(buf, RMCFG_ALL);
			}
		} else {
			rm_cfgopt("dead-zone", RMCFG_OWN);
		}
	} else {
		for(i=0; i<MAX_AXES; i++) {
			if(cfg->dead_threshold[i] != def.dead_threshold[i]) {
				add_cfgopt(CFG_DEADZONE_N, i, "dead-zone%d = %d", i, cfg->dead_threshold[i]);
				rm_cfgopt("dead-zone", RMCFG_ALL);
			} else {
				sprintf(buf, "dead-zone%d", i);
				rm_cfgopt(buf, RMCFG_OWN);
			}
		}
	}

	if(cfg->repeat_msec != def.repeat_msec) {
		add_cfgopt(CFG_REPEAT, 0, "repeat-interval = %d\n", cfg->repeat_msec);
	} else {
		rm_cfgopt("repeat-interval", RMCFG_ALL);
	}

	if(cfg->invert[0] || cfg->invert[1] || cfg->invert[2]) {
		char flags[4] = {0}, *p = flags;
		if(cfg->invert[0]) *p++ = 'x';
		if(cfg->invert[1]) *p++ = 'y';
		if(cfg->invert[2]) *p = 'z';
		add_cfgopt(CFG_INVTRANS, 0, "invert-trans = %s", flags);
	} else {
		rm_cfgopt("invert-trans", RMCFG_ALL);
	}

	if(cfg->invert[3] || cfg->invert[4] || cfg->invert[5]) {
		char flags[4] = {0}, *p = flags;
		if(cfg->invert[3]) *p++ = 'x';
		if(cfg->invert[4]) *p++ = 'y';
		if(cfg->invert[5]) *p = 'z';
		add_cfgopt(CFG_INVROT, 0, "invert-rot = %s", flags);
	} else {
		rm_cfgopt("invert-rot", RMCFG_ALL);
	}

	if(cfg->swapyz) {
		add_cfgopt(CFG_SWAPYZ, 0, "swap-yz = true");
	} else {
		rm_cfgopt("swap-yz", RMCFG_ALL);
	}

	for(i=0; i<MAX_BUTTONS; i++) {
		if(cfg->map_button[i] != i) {
			add_cfgopt(CFG_BNMAP_N, i, "bnmap%d = %d", i, cfg->map_button[i]);
		} else {
			sprintf(buf, "bnmap%d", i);
			rm_cfgopt(buf, RMCFG_ALL);
		}
	}

	for(i=0; i<MAX_BUTTONS; i++) {
		if(cfg->bnact[i] != BNACT_NONE) {
			add_cfgopt(CFG_BNACT_N, i, "bnact%d = %s", i, bnact_name(cfg->bnact[i]));
		} else {
			sprintf(buf, "bnact%d", i);
			rm_cfgopt(buf, RMCFG_ALL);
		}
	}

	for(i=0; i<MAX_BUTTONS; i++) {
		if(cfg->kbmap_str[i]) {
			add_cfgopt(CFG_KBMAP_N, i, "kbmap%d = %s", i, cfg->kbmap_str[i]);
		} else {
			sprintf(buf, "kbmap%d", i);
			rm_cfgopt(buf, RMCFG_ALL);
		}
	}

	if(cfg->led != def.led) {
		add_cfgopt(CFG_LED, 0, "led = %s", (cfg->led ? (cfg->led == LED_AUTO ? "auto" : "on") : "off"));
	} else {
		rm_cfgopt("led", RMCFG_OWN);
	}

	if(cfg->grab_device != def.grab_device) {
		add_cfgopt(CFG_GRAB, 0, "grab = %s", cfg->grab_device ? "true" : "false");
	} else {
		rm_cfgopt("grab", RMCFG_OWN);
	}

	if(cfg->serial_dev[0]) {
		add_cfgopt(CFG_SERIAL, 0, "serial = %s", cfg->serial_dev);
	} else {
		rm_cfgopt("serial", RMCFG_ALL);
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
		if(!cfglines[i].str) continue;

		if(*cfglines[i].str) {
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

/* profile switching */
extern void cfg_changed(void);

int switch_profile(int profile_idx)
{
	int i;
	struct cfg new_cfg;

	/* validate profile index */
	if(profile_idx < -1 || profile_idx >= MAX_PROFILES) {
		logmsg(LOG_ERR, "invalid profile index: %d\n", profile_idx);
		return -1;
	}

	/* check if already on requested profile (no-op) */
	if(current_profile == profile_idx) {
		return 0;
	}

	/* switching to default profile */
	if(profile_idx == -1) {
		logmsg(LOG_INFO, "==== Switching to DEFAULT profile ====\n");
		/* restore from backup, preserving profile definitions */
		cfg = default_cfg_backup;
		current_profile = -1;
		cfg_changed();
		return 0;
	}

	/* switching to named profile */
	if(profile_idx >= default_cfg_backup.num_profiles) {
		logmsg(LOG_ERR, "profile %d not defined\n", profile_idx);
		return -1;
	}

	logmsg(LOG_INFO, "==== Switching to profile: %s ====\n",
		default_cfg_backup.profiles[profile_idx].name);

	/* start with main config as base, then apply profile overrides */
	new_cfg = default_cfg_backup;
	apply_profile_overrides(&new_cfg, &default_cfg_backup.profiles[profile_idx]);

	/* apply new config */
	/* free old kbmap strings */
	for(i=0; i<MAX_BUTTONS; i++) {
		if(cfg.kbmap_str[i] && cfg.kbmap_str[i] != default_cfg_backup.kbmap_str[i]) {
			free(cfg.kbmap_str[i]);
		}
	}

	cfg = new_cfg;
	current_profile = profile_idx;
	cfg_changed();

	return 0;
}

int get_current_profile(void)
{
	return current_profile;
}

const char *get_profile_name(int profile_idx)
{
	if(profile_idx < 0 || profile_idx >= default_cfg_backup.num_profiles) {
		return NULL;
	}
	return default_cfg_backup.profiles[profile_idx].name;
}

/* profile management with app-based switching */
static int previous_profile = -1;  /* track profile before app-triggered switch */

int find_profile_by_appname(const char *appname)
{
	int i;

	if(!appname) {
		return -1;
	}

	for(i=0; i<default_cfg_backup.num_profiles; i++) {
		if(default_cfg_backup.profiles[i].appname &&
		   strcmp(default_cfg_backup.profiles[i].appname, appname) == 0) {
			return i;
		}
	}

	return -1;  /* no matching profile found */
}

void switch_profile_by_appname(const char *appname)
{
	int profile_idx;

	if(!appname) {
		return;
	}

	profile_idx = find_profile_by_appname(appname);
	if(profile_idx == -1) {
		logmsg(LOG_INFO, "No profile configured for application: %s\n", appname);
		return;
	}

	/* save current profile before app-triggered switch */
	previous_profile = current_profile;

	logmsg(LOG_INFO, "==== Application '%s' detected, auto-switching to profile '%s' ====\n",
		appname, default_cfg_backup.profiles[profile_idx].name);

	switch_profile(profile_idx);
}

void restore_previous_profile(void)
{
	if(previous_profile == current_profile) {
		/* nothing to restore */
		return;
	}

	logmsg(LOG_INFO, "==== Application disconnected, restoring previous profile ====\n");

	if(previous_profile == -1) {
		logmsg(LOG_INFO, "Restoring DEFAULT profile\n");
	} else {
		logmsg(LOG_INFO, "Restoring profile: %s\n",
			get_profile_name(previous_profile));
	}

	switch_profile(previous_profile);
	previous_profile = -1;
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
	{"dominant-axis", BNACT_DOMINANT_AXIS},
	{"profile-0", BNACT_PROFILE_0},
	{"profile-1", BNACT_PROFILE_1},
	{"profile-2", BNACT_PROFILE_2},
	{"profile-3", BNACT_PROFILE_3},
	{"profile-4", BNACT_PROFILE_4},
	{"profile-5", BNACT_PROFILE_5},
	{"profile-6", BNACT_PROFILE_6},
	{"profile-7", BNACT_PROFILE_7},
	{"profile-8", BNACT_PROFILE_8},
	{"profile-9", BNACT_PROFILE_9},
	{"profile-10", BNACT_PROFILE_10},
	{"profile-11", BNACT_PROFILE_11},
	{"profile-12", BNACT_PROFILE_12},
	{"profile-13", BNACT_PROFILE_13},
	{"profile-14", BNACT_PROFILE_14},
	{"profile-15", BNACT_PROFILE_15},
	{0, 0}
};

static int parse_bnact(const char *s, struct cfg *cfg)
{
	int i;

	/* First check if it matches a profile name */
	if(cfg) {
		for(i=0; i<cfg->num_profiles; i++) {
			if(cfg->profiles[i].name && strcmp(cfg->profiles[i].name, s) == 0) {
				/* Found matching profile, return corresponding BNACT_PROFILE_N */
				return BNACT_PROFILE_0 + i;
			}
		}
	}

	/* Check against standard button action names */
	for(i=0; bnact_strtab[i].name; i++) {
		if(strcmp(bnact_strtab[i].name, s) == 0) {
			return bnact_strtab[i].act;
		}
	}
	return -1;
}

static const char *bnact_name(int bnact)
{
	int i;
	for(i=0; bnact_strtab[i].name; i++) {
		if(bnact_strtab[i].act == bnact) {
			return bnact_strtab[i].name;
		}
	}
	return "none";
}

static int parse_kbmap(const char *str, unsigned int *kbmap, int max_keys)
{
#ifdef USE_X11
	char buf[256], *ptr, *start;
	int count = 0;

	if(!str || !*str) return 0;

	strncpy(buf, str, sizeof buf - 1);
	buf[sizeof buf - 1] = 0;

	start = buf;
	while(*start && count < max_keys) {
		ptr = strchr(start, '+');
		if(ptr) *ptr = 0;

		while(*start && isspace(*start)) start++;
		if(*start) {
			char *end = start + strlen(start) - 1;
			while(end > start && isspace(*end)) *end-- = 0;
		}

		if(*start) {
			unsigned int ksym = kbemu_keysym(start);
			if(ksym == 0) {
				logmsg(LOG_WARNING, "invalid key name in keyboard mapping: \"%s\"\n", start);
			} else {
				kbmap[count++] = ksym;
			}
		}

		if(!ptr) break;
		start = ptr + 1;
	}

	if(count >= max_keys && *start) {
		logmsg(LOG_WARNING, "keyboard mapping truncated, max %d keys supported\n", max_keys);
	}

	return count;
#else
	return 0;
#endif
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
		lptr = cfglines + num_lines++;
		lptr->own = 1;
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

static int rm_cfgopt(const char *name, int mode)
{
	int i;
	char *ptr, *endp;
	char buf[256];

	for(i=0; i<num_lines; i++) {
		if(!cfglines[i].str || !*cfglines[i].str) continue;

		strncpy(buf, cfglines[i].str, sizeof buf - 1);
		buf[sizeof buf - 1] = 0;

		ptr = buf;
		while(*ptr && isspace(*ptr)) ptr++;
		if(!(endp = strchr(ptr, '='))) {
			continue;
		}
		while(endp > ptr && isspace(*--endp)) *endp = 0;
		if(strcmp(ptr, name) == 0) {
			if(mode != RMCFG_OWN || cfglines[i].own) {
				free(cfglines[i].str);
				cfglines[i].str = 0;
			}
			return 0;
		}
	}
	return -1;
}
