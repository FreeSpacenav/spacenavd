#ifndef SPNAVD_H_
#define SPNAVD_H_

#include "cfgfile.h"

#define SOCK_NAME	"/var/run/spnav.sock"
#define PIDFILE		"/var/run/spnavd.pid"
#define LOGFILE		"/var/log/spnavd.log"

struct cfg cfg;
int verbose;

#endif	/* SPNAVD_H_ */
