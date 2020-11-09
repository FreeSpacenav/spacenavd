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
#include "userpriv.h"

enum {TX, TY, TZ, RX, RY, RZ};

static const int def_axmap[] = {0, 2, 1, 3, 5, 4};
static const int def_axinv[] = {0, 1, 1, 0, 1, 1};

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
		cfg->invert[i] = def_axinv[i];
		cfg->map_axis[i] = def_axmap[i];
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
	char buf[512];
	struct flock flk;
	int num_devid = 0;
	/*int num_devnames = 0;*/

        int username_from_cfg = 0, groupname_from_cfg = 0;

	default_cfg(cfg);

	logmsg(LOG_INFO, "reading config file: %s\n", fname);
	if(!(fp = fopen(fname, "r"))) {
		logmsg(LOG_WARNING, "failed to open config file %s: %s. using defaults.\n", fname, strerror(errno));
		return -1;
	}

	/* acquire shared read lock */
	flk.l_type = F_RDLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	while(fcntl(fileno(fp), F_SETLKW, &flk) == -1);

	while(fgets(buf, sizeof buf, fp)) {
		int isint, isfloat, ival, i, bnidx, axisidx;
		float fval;
		char *endp, *key_str, *val_str, *line = buf;
		while(*line == ' ' || *line == '\t') line++;

		if(!*line || *line == '\n' || *line == '\r' || *line == '#') {
			continue;
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
			EXPECT(isint);
			cfg->repeat_msec = ival;

		} else if(strcmp(key_str, "dead-zone") == 0) {
			EXPECT(isint);
			for(i=0; i<6; i++) {
				cfg->dead_threshold[i] = ival;
			}

		} else if(strcmp(key_str, "dead-zone-translation-x") == 0) {
			EXPECT(isint);
			cfg->dead_threshold[0] = ival;

		} else if(strcmp(key_str, "dead-zone-translation-y") == 0) {
			EXPECT(isint);
			cfg->dead_threshold[1] = ival;

		} else if(strcmp(key_str, "dead-zone-translation-z") == 0) {
			EXPECT(isint);
			cfg->dead_threshold[2] = ival;

		} else if(strcmp(key_str, "dead-zone-rotation-x") == 0) {
			EXPECT(isint);
			cfg->dead_threshold[3] = ival;

		} else if(strcmp(key_str, "dead-zone-rotation-y") == 0) {
			EXPECT(isint);
			cfg->dead_threshold[4] = ival;

		} else if(strcmp(key_str, "dead-zone-rotation-z") == 0) {
			EXPECT(isint);
			cfg->dead_threshold[5] = ival;

		} else if(strcmp(key_str, "sensitivity") == 0) {
			EXPECT(isfloat);
			cfg->sensitivity = fval;

		} else if(strcmp(key_str, "sensitivity-translation") == 0) {
			EXPECT(isfloat);
			cfg->sens_trans[0] = cfg->sens_trans[1] = cfg->sens_trans[2] = fval;

		} else if(strcmp(key_str, "sensitivity-translation-x") == 0) {
			EXPECT(isfloat);
			cfg->sens_trans[0] = fval;

		} else if(strcmp(key_str, "sensitivity-translation-y") == 0) {
			EXPECT(isfloat);
			cfg->sens_trans[1] = fval;

		} else if(strcmp(key_str, "sensitivity-translation-z") == 0) {
			EXPECT(isfloat);
			cfg->sens_trans[2] = fval;

		} else if(strcmp(key_str, "sensitivity-rotation") == 0) {
			EXPECT(isfloat);
			cfg->sens_rot[0] = cfg->sens_rot[1] = cfg->sens_rot[2] = fval;

		} else if(strcmp(key_str, "sensitivity-rotation-x") == 0) {
			EXPECT(isfloat);
			cfg->sens_rot[0] = fval;

		} else if(strcmp(key_str, "sensitivity-rotation-y") == 0) {
			EXPECT(isfloat);
			cfg->sens_rot[1] = fval;

		} else if(strcmp(key_str, "sensitivity-rotation-z") == 0) {
			EXPECT(isfloat);
			cfg->sens_rot[2] = fval;

		} else if(strcmp(key_str, "invert-rot") == 0) {
			if(strchr(val_str, 'x')) {
				cfg->invert[RX] = !def_axinv[RX];
			}
			if(strchr(val_str, 'y')) {
				cfg->invert[RY] = !def_axinv[RY];
			}
			if(strchr(val_str, 'z')) {
				cfg->invert[RZ] = !def_axinv[RZ];
			}

		} else if(strcmp(key_str, "invert-trans") == 0) {
			if(strchr(val_str, 'x')) {
				cfg->invert[TX] = !def_axinv[TX];
			}
			if(strchr(val_str, 'y')) {
				cfg->invert[TY] = !def_axinv[TY];
			}
			if(strchr(val_str, 'z')) {
				cfg->invert[TZ] = !def_axinv[TZ];
			}

		} else if(strcmp(key_str, "swap-yz") == 0) {
			int i, swap_yz = 0;

			if(isint) {
				swap_yz = ival;
			} else {
				if(strcmp(val_str, "true") == 0 || strcmp(val_str, "on") == 0 || strcmp(val_str, "yes") == 0) {
					swap_yz = 1;
				} else if(strcmp(val_str, "false") == 0 || strcmp(val_str, "off") == 0 || strcmp(val_str, "no") == 0) {
					swap_yz = 0;
				} else {
					logmsg(LOG_WARNING, "invalid configuration value for %s, expected a boolean value.\n", key_str);
					continue;
				}
			}

			for(i=0; i<6; i++) {
				cfg->map_axis[i] = swap_yz ? i : def_axmap[i];
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
			cfg->map_button[bnidx] = ival;

		} else if(sscanf(key_str, "kbmap%d", &bnidx) == 1) {
			if(bnidx < 0 || bnidx >= MAX_BUTTONS) {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a number from 0 to %d\n", key_str, MAX_BUTTONS);
				continue;
			}
			if(cfg->kbmap_str[bnidx]) {
				logmsg(LOG_WARNING, "warning: multiple keyboard mappings for button %d: %s -> %s\n", bnidx, cfg->kbmap_str[bnidx], val_str);
				free(cfg->kbmap_str[bnidx]);
			}
			cfg->kbmap_str[bnidx] = strdup(val_str);

		} else if(strcmp(key_str, "led") == 0) {
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
			strncpy(cfg->serial_dev, val_str, PATH_MAX - 1);

		} else if(strcmp(key_str, "device-id") == 0) {
			unsigned int vendor, prod;
			if(sscanf(val_str, "%x:%x", &vendor, &prod) == 2) {
				cfg->devid[num_devid][0] = (int)vendor;
				cfg->devid[num_devid][1] = (int)prod;
				num_devid++;
			} else {
				logmsg(LOG_WARNING, "invalid configuration value for %s, expected a vendorid:productid pair\n", key_str);
				continue;
			}

		} else if(strcmp(key_str, "user") == 0) {
                    if(user_set_by_cmdline() == 1) {
                        logmsg(LOG_WARNING, "started with -u option - ignoring user setting in configfile\n");
                    } else if(set_runas_uid (val_str) == 0) {
                        logmsg(LOG_WARNING, "invalid configuration value for %s, expected a valid username (%s doesn't exists) - resuming with invoked user\n", key_str, val_str);
                    } else {
                        username_from_cfg = 1;
                    }
                } else if(strcmp(key_str, "group") == 0) {
                    if(group_set_by_cmdline() == 1) {
                        logmsg(LOG_WARNING, "started with -g option - ignoring group setting in configfile\n");
                    } else if(set_runas_gid (val_str) == 0) {
                        logmsg(LOG_WARNING, "invalid configuration value for %s, expected a valid groupname (%s doesn't exists) - resuming with invoked group\n", key_str, val_str);
                    } else {
                        groupname_from_cfg = 1;
                    }
                } else {
			logmsg(LOG_WARNING, "unrecognized config option: %s\n", key_str);
		}
	}

	/* unlock */
	flk.l_type = F_UNLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	fcntl(fileno(fp), F_SETLK, &flk);

	fclose(fp);

        if(username_from_cfg || groupname_from_cfg) {
            test_initial_user_privileges();
            start_daemon_privileges();
        }

	return 0;
}

int write_cfg(const char *fname, struct cfg *cfg)
{
	int i, wrote_comment;
	FILE *fp;
	struct flock flk;

	if(!(fp = fopen(fname, "w"))) {
		logmsg(LOG_ERR, "failed to write config file %s: %s\n", fname, strerror(errno));
		return -1;
	}

	/* acquire exclusive write lock */
	flk.l_type = F_WRLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	while(fcntl(fileno(fp), F_SETLKW, &flk) == -1);

	fprintf(fp, "# sensitivity is multiplied with every motion (1.0 normal).\n");
	fprintf(fp, "sensitivity = %.3f\n\n", cfg->sensitivity);

	fprintf(fp, "# separate sensitivity for rotation and translation.\n");

	if(cfg->sens_trans[0] == cfg->sens_trans[1] && cfg->sens_trans[1] == cfg->sens_trans[2]) {
		fprintf(fp, "sensitivity-translation = %.3f\n", cfg->sens_trans[0]);
	} else {
		fprintf(fp, "sensitivity-translation-x = %.3f\n", cfg->sens_trans[0]);
		fprintf(fp, "sensitivity-translation-y = %.3f\n", cfg->sens_trans[1]);
		fprintf(fp, "sensitivity-translation-z = %.3f\n", cfg->sens_trans[2]);
	}

	if(cfg->sens_rot[0] == cfg->sens_rot[1] && cfg->sens_rot[1] == cfg->sens_rot[2]) {
		fprintf(fp, "sensitivity-rotation = %.3f\n", cfg->sens_rot[0]);
	} else {
		fprintf(fp, "sensitivity-rotation-x = %.3f\n", cfg->sens_rot[0]);
		fprintf(fp, "sensitivity-rotation-y = %.3f\n", cfg->sens_rot[1]);
		fprintf(fp, "sensitivity-rotation-z = %.3f\n", cfg->sens_rot[2]);
	}
	fputc('\n', fp);

	fprintf(fp, "# dead zone; any motion less than this number, is discarded as noise.\n");

	if(cfg->dead_threshold[0] == cfg->dead_threshold[1] && cfg->dead_threshold[1] == cfg->dead_threshold[2] && cfg->dead_threshold[2] == cfg->dead_threshold[3] && cfg->dead_threshold[3] == cfg->dead_threshold[4] && cfg->dead_threshold[4] == cfg->dead_threshold[5]) {
		fprintf(fp, "dead-zone = %d\n", cfg->dead_threshold[0]);
	} else {
		fprintf(fp, "dead-zone-translation-x = %d\n", cfg->dead_threshold[0]);
		fprintf(fp, "dead-zone-translation-y = %d\n", cfg->dead_threshold[1]);
		fprintf(fp, "dead-zone-translation-z = %d\n", cfg->dead_threshold[2]);
		fprintf(fp, "dead-zone-rotation-x = %d\n", cfg->dead_threshold[3]);
		fprintf(fp, "dead-zone-rotation-y = %d\n", cfg->dead_threshold[4]);
		fprintf(fp, "dead-zone-rotation-z = %d\n", cfg->dead_threshold[5]);
	}
	fputc('\n', fp);

	fprintf(fp, "# repeat interval; non-deadzone events are repeated every so many milliseconds (-1 to disable)\n");
	fprintf(fp, "repeat-interval = %d\n", cfg->repeat_msec);

	if(cfg->invert[0] != def_axinv[0] || cfg->invert[1] != def_axinv[1] || cfg->invert[2] != def_axinv[2]) {
		fprintf(fp, "# invert translations on some axes.\n");
		fprintf(fp, "invert-trans = ");
		if(cfg->invert[0] != def_axinv[0]) fputc('x', fp);
		if(cfg->invert[1] != def_axinv[1]) fputc('y', fp);
		if(cfg->invert[2] != def_axinv[2]) fputc('z', fp);
		fputs("\n\n", fp);
	}

	if(cfg->invert[3] != def_axinv[3] || cfg->invert[4] != def_axinv[4] || cfg->invert[5] != def_axinv[5]) {
		fprintf(fp, "# invert rotations around some axes.\n");
		fprintf(fp, "invert-rot = ");
		if(cfg->invert[3] != def_axinv[3]) fputc('x', fp);
		if(cfg->invert[4] != def_axinv[4]) fputc('y', fp);
		if(cfg->invert[5] != def_axinv[5]) fputc('z', fp);
		fputs("\n\n", fp);
	}

	fprintf(fp, "# swap translation along Y and Z axes\n");
	fprintf(fp, "swap-yz = %s\n\n", cfg->map_axis[1] == def_axmap[1] ? "false" : "true");

	wrote_comment = 0;
	for(i=0; i<MAX_BUTTONS; i++) {
		if(cfg->map_button[i] != i) {
			if(!wrote_comment) {
				fprintf(fp, "# button mappings\n");
				wrote_comment = 1;
			}
			fprintf(fp, "bnmap%d = %d\n", i, cfg->map_button[i]);
		}
	}
	if(wrote_comment) {
		fputc('\n', fp);
	}

	wrote_comment = 0;
	for(i=0; i<MAX_BUTTONS; i++) {
		if(cfg->kbmap_str[i]) {
			if(!wrote_comment) {
				fprintf(fp, "# button to key mappings\n");
				wrote_comment = 1;
			}
			fprintf(fp, "kbmap%d = %s\n", i, cfg->kbmap_str[i]);
		}
	}
	if(wrote_comment) {
		fputc('\n', fp);
	}

	fprintf(fp, "# led status: on, off, or auto (turn on when a client is connected)\n");
	fprintf(fp, "led = %s\n\n", (cfg->led ? (cfg->led == LED_AUTO ? "auto" : "on") : "off"));

	if(!cfg->grab_device) {
		fprintf(fp, "# Don't grab USB device\n");
		fprintf(fp, "#   Grabbing the device ensures that other programs won't be able to use it without\n");
		fprintf(fp, "#   talking to spacenavd. For instance some versions of Xorg will use the device to move\n");
		fprintf(fp, "#   the mouse pointer if we don't grab it.\n");
		fprintf(fp, "#   Set this to false if you want to use programs that try to talk to the device directly\n");
		fprintf(fp, "#   such as google earth, then follow FAQ 11 http://spacenav.sourceforge.net/faq.html#faq11\n");
		fprintf(fp, "#   to force the X server to ignore the device\n");
		fprintf(fp, "grab = false\n\n");
	}

	fprintf(fp, "# serial device\n");
	fprintf(fp, "#   Set this only if you have a serial device, and make sure you specify the\n");
	fprintf(fp, "#   correct device file (On linux usually: /dev/ttyS0, /dev/ttyS1, /dev/ttyUSB0 ... etc).\n");
	if(cfg->serial_dev[0]) {
		fprintf(fp, "serial = %s\n\n", cfg->serial_dev);
	} else {
		fprintf(fp, "#serial = /dev/ttyS0\n\n");
	}

	fprintf(fp, "List of additional USB devices to use (multiple devices can be listed)");
	for(i=0; i<MAX_CUSTOM; i++) {
		if(cfg->devid[i][0] != -1 && cfg->devid[i][1] != -1) {
			fprintf(fp, "device-id = %x:%x\n", cfg->devid[i][0], cfg->devid[i][1]);
		}
	}
	fprintf(fp, "\n");

	/* unlock */
	flk.l_type = F_UNLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	fcntl(fileno(fp), F_SETLK, &flk);

	fclose(fp);
	return 0;
}
