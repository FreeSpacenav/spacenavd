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
#include <stdarg.h>
#include <syslog.h>
#include "logger.h"

static FILE *logfile;
static int use_syslog;

int start_logfile(const char *fname)
{
	if(!(logfile = fopen(fname, "w"))) {
		fprintf(stderr, "failed to open log file: %s\n", fname);
		return -1;
	}
	setvbuf(logfile, 0, _IONBF, 0);
	return fileno(logfile);
}

int start_syslog(const char *id)
{
	openlog(id, LOG_NDELAY, LOG_DAEMON);
	use_syslog = 1;
	return 0;
}

void logmsg(int prio, const char *fmt, ...)
{
	va_list ap;

	if(use_syslog) {
		va_start(ap, fmt);
		vsyslog(prio, fmt, ap);
		va_end(ap);
	}
	va_start(ap, fmt);
	vfprintf(logfile ? logfile : stdout, fmt, ap);
	va_end(ap);
}
