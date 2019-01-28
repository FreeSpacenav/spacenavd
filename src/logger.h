#ifndef LOGGER_H_
#define LOGGER_H_

#include <syslog.h>	/* for log priority levels */

int start_logfile(const char *fname);
int start_syslog(const char *id);

void logmsg(int prio, const char *fmt, ...);

#endif	/* LOGGER_H_ */
