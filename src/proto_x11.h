#ifndef PROTO_X11_H_
#define PROTO_X11_H_

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "event.h"
#include "client.h"

int init_x11(void);
void close_x11(void);

int get_x11_socket(void);

void send_xevent(spnav_event *ev, struct client *c);
int handle_xevents(fd_set *rset);

void set_client_window(Window win);
void remove_client_window(Window win);


#endif	/* PROTO_X11_H_ */
