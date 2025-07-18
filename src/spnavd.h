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

#ifndef SPNAVD_H_
#define SPNAVD_H_

#include "config.h"
#include "cfgfile.h"
#include "logger.h"

#define DEF_CFGFILE		CFGDIR "/spnavrc"
#define DEF_LOGFILE		"/var/log/spnavd.log"
#define DEF_PIDFILE		"/var/run/spnavd.pid"

#define SOCK_NAME	"/var/run/spnav.sock"
#define SYSLOG_ID	"spnavd"

/* Multiple devices support */
#ifndef MAX_DEVICES
#define MAX_DEVICES 8
#endif

#if defined(__cplusplus) || (__STDC_VERSION__ >= 199901L)
#define INLINE	inline
#else	/* not C++ or C99 */

#ifdef __GNUC__
#define INLINE	__inline__
#else
#define INLINE
#endif

#endif


/* defined in cfgfile.c */
extern struct cfg cfg, prev_cfg;

/* defined in spnavd.c */
extern int verbose;
extern char *cfgfile;


void cfg_changed(void);

#endif	/* SPNAVD_H_ */
