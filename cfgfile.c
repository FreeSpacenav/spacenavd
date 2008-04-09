/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007 John Tsiombikas <nuclear@siggraph.org>

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

enum {TX, TY, TZ, RX, RY, RZ};

void default_cfg(struct cfg *cfg)
{
	cfg->sensitivity = 1.0;
	cfg->dead_threshold = 2;
	memset(cfg->invert, 0, sizeof cfg->invert);
}

int read_cfg(const char *fname, struct cfg *cfg)
{
	FILE *fp;
	char buf[512];
	struct flock flk;

	default_cfg(cfg);

	if(!(fp = fopen(fname, "r"))) {
		fprintf(stderr, "failed to open config file %s: %s. using defaults.\n", fname, strerror(errno));
		return -1;
	}

	/* aquire shared read lock */
	flk.l_type = F_RDLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	while(fcntl(fileno(fp), F_SETLKW, &flk) == -1);

	while(fgets(buf, sizeof buf, fp)) {
		int isnum;
		char *key_str, *val_str, *line = buf;
		while(*line == ' ' || *line == '\t') line++;

		if(!*line || *line == '\n' || *line == '\r' || *line == '#') {
			continue;
		}

		if(!(key_str = strtok(line, " :=\n\t\r"))) {
			fprintf(stderr, "invalid config line: %s, skipping.\n", line);
			continue;
		}
		if(!(val_str = strtok(0, " :=\n\t\r"))) {
			fprintf(stderr, "missing value for config key: %s\n", key_str);
			continue;
		}

		isnum = isdigit(val_str[0]);

		if(strcmp(key_str, "dead-zone") == 0) {
			if(!isnum) {
				fprintf(stderr, "invalid configuration value for %s, expected a number.\n", key_str);
				continue;
			}
			cfg->dead_threshold = atoi(val_str);

		} else if(strcmp(key_str, "sensitivity") == 0) {
			if(!isnum) {
				fprintf(stderr, "invalid configuration value for %s, expected a number.\n", key_str);
				continue;
			}
			cfg->sensitivity = atof(val_str);

		} else if(strcmp(key_str, "invert-rot") == 0) {
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
			if(strchr(val_str, 'x')) {
				cfg->invert[TX] = 1;
			}
			if(strchr(val_str, 'y')) {
				cfg->invert[TY] = 1;
			}
			if(strchr(val_str, 'z')) {
				cfg->invert[TZ] = 1;
			}

		} else {
			fprintf(stderr, "unrecognized config option: %s\n", key_str);
		}
	}

	/* unlock */
	flk.l_type = F_UNLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	fcntl(fileno(fp), F_SETLK, &flk);

	fclose(fp);
	return 0;
}

int write_cfg(const char *fname, struct cfg *cfg)
{
	FILE *fp;
	struct flock flk;

	if(!(fp = fopen(fname, "w"))) {
		fprintf(stderr, "failed to write config file %s: %s\n", fname, strerror(errno));
		return -1;
	}

	/* aquire exclusive write lock */
	flk.l_type = F_WRLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	while(fcntl(fileno(fp), F_SETLKW, &flk) == -1);

	fprintf(fp, "# sensitivity is multiplied with every motion (1.0 normal).\n");
	fprintf(fp, "sensitivity = %.3f\n\n", cfg->sensitivity);

	fprintf(fp, "# dead zone; any motion less than this number, is discarded as noise.\n");
	fprintf(fp, "dead-zone = %d\n\n", cfg->dead_threshold);

	if(cfg->invert[0] || cfg->invert[1] || cfg->invert[2]) {
		fprintf(fp, "# invert translations on some axes.\n");
		fprintf(fp, "invert-trans = ");
		if(cfg->invert[0]) fputc('x', fp);
		if(cfg->invert[1]) fputc('y', fp);
		if(cfg->invert[2]) fputc('z', fp);
		fputs("\n\n", fp);
	}

	if(cfg->invert[3] || cfg->invert[4] || cfg->invert[5]) {
		fprintf(fp, "# invert rotations around some axes.\n");
		fprintf(fp, "invert-rot = ");
		if(cfg->invert[3]) fputc('x', fp);
		if(cfg->invert[4]) fputc('y', fp);
		if(cfg->invert[5]) fputc('z', fp);
		fputs("\n\n", fp);
	}

	/* unlock */
	flk.l_type = F_UNLCK;
	flk.l_start = flk.l_len = 0;
	flk.l_whence = SEEK_SET;
	fcntl(fileno(fp), F_SETLK, &flk);

	fclose(fp);
	return 0;
}
