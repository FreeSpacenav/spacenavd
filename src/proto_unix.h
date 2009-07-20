#ifndef PROTO_UNIX_H_
#define PROTO_UNIX_H_

#include "event.h"
#include "client.h"

int init_unix(void);
int get_unix_socket(void);

void send_uevent(spnav_event *ev, struct client *c);

int handle_uevents(fd_set *rset);

#endif	/* PROTO_UNIX_H_ */
