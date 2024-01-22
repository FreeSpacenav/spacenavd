/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2010 John Tsiombikas <nuclear@member.fsf.org>

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

#ifndef PROTO_UNIX_H_
#define PROTO_UNIX_H_

#include "config.h"
#include "event.h"
#include "client.h"

int init_unix(void);
void close_unix(void);
int get_unix_socket(void);

void send_uevent(struct device *dev, spnav_event *ev, struct client *c);

int handle_uevents(fd_set *rset);

#endif	/* PROTO_UNIX_H_ */
