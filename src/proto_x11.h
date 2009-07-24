/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2009 John Tsiombikas <nuclear@member.fsf.org>

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

#ifndef PROTO_X11_H_
#define PROTO_X11_H_

#include "config.h"
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
