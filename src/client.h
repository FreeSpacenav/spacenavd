/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2022 John Tsiombikas <nuclear@member.fsf.org>

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

#ifndef CLIENT_H_
#define CLIENT_H_

#include "config.h"

#ifdef USE_X11
#include <X11/Xlib.h>
#endif

/* client types */
enum {
	CLIENT_X11,		/* through the magellan X11 protocol */
	CLIENT_UNIX		/* through the new UNIX domain socket */
};

struct device;

struct client {
	int type;

	int sock;	/* UNIX domain socket */
	int proto;	/* protocol version */
#ifdef USE_X11
	Window win;	/* X11 client window */
#endif

	float sens;	/* sensitivity */
	struct device *dev;

	char reqbuf[64];
	int reqbytes;

	struct client *next;
};

struct client *add_client(int type, void *cdata);
void remove_client(struct client *client);

int get_client_type(struct client *client);
int get_client_socket(struct client *client);
#ifdef USE_X11
Window get_client_window(struct client *client);
#endif

void set_client_sensitivity(struct client *client, float sens);
float get_client_sensitivity(struct client *client);

void set_client_device(struct client *client, struct device *dev);
struct device *get_client_device(struct client *client);

/* these two can be used to iterate over all clients */
struct client *first_client(void);
struct client *next_client(void);


#endif	/* CLIENT_H_ */
