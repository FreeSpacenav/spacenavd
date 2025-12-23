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

#ifndef CLIENT_H_
#define CLIENT_H_

#include "config.h"

#ifdef USE_X11
#include <X11/Xlib.h>
#endif

#include "proto.h"

/* client types */
enum {
	CLIENT_X11,		/* through the magellan X11 protocol */
	CLIENT_UNIX		/* through the new UNIX domain socket */
};

/* event selection (must match SPNAV_EVMASK* in libspnav/spnav.h) */
enum {
	EVMASK_MOTION		= 0x01,
	EVMASK_BUTTON		= 0x02,
	EVMASK_DEV			= 0x04,
	EVMASK_CFG			= 0x08,
	EVMASK_RAWAXIS		= 0x10,
	EVMASK_RAWBUTTON	= 0x20
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

	char *name;				/* client name (not unique) */
	unsigned int evmask;	/* event selection mask */

	char reqbuf[64];
	int reqbytes;

	/* protocol buffer for handling reception of strings in multiple packets */
	struct reqresp_strbuf strbuf;

	struct client *next;
};

struct client *add_client(int type, void *cdata);
void remove_client(struct client *client);
void free_client(struct client *client);

int get_client_type(struct client *client);
int get_client_socket(struct client *client);
#ifdef USE_X11
Window get_client_window(struct client *client);
#endif

void set_client_sensitivity(struct client *client, float sens);
float get_client_sensitivity(struct client *client);

void set_client_device(struct client *client, struct device *dev);
struct device *get_client_device(struct client *client);

/* if client has has enabled multi-device mode, then return 1, otherwise 0*/
int is_client_multidev(struct client *client);

/* these two can be used to iterate over all clients */
struct client *first_client(void);
struct client *next_client(void);


#endif	/* CLIENT_H_ */
