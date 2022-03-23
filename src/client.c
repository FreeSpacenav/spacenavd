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

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include "client.h"
#include "dev.h"
#include "spnavd.h"

#ifdef USE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

static struct client *client_list = NULL;
static struct client *client_iter;	/* iterator (used by first/next calls) */

/* add a client to the list
 * cdata points to the socket fd for new-protocol clients, or the
 * window XID for clients talking to us through the magellan protocol
 */
struct client *add_client(int type, void *cdata)
{
	struct client *client;

#ifdef USE_X11
	if(!cdata || (type != CLIENT_UNIX && type != CLIENT_X11))
#else
	if(!cdata || type != CLIENT_UNIX)
#endif
	{
		return 0;
	}

	if(!(client = calloc(1, sizeof *client))) {
		return 0;
	}

	client->type = type;
	if(type == CLIENT_UNIX) {
		client->sock = *(int*)cdata;
#ifdef USE_X11
	} else {
		client->win = *(Window*)cdata;
#endif
	}
	/* default to protocol version 0 until the client changes it */
	client->proto = 0;
	/* evmask for proto-v0 clients is just input events */
	client->evmask = EVMASK_MOTION | EVMASK_BUTTON;

	client->sens = 1.0f;
	client->dev = 0; /* default/first device */

	if(!client_list && cfg.led == LED_AUTO) {
		/* on first client, turn the led on */
		set_devices_led(1);
	}
	client->next = client_list;
	client_list = client;

	return client;
}

void remove_client(struct client *client)
{
	struct client *iter = client_list;
	if(!iter) return;

	if(iter == client) {
		client_list = iter->next;
		free_client(iter);
		iter = client_list;
		if(!iter) {
			if(cfg.led == LED_AUTO) {
				set_devices_led(0); /* no more clients, turn off led */
			}
			return;
		}
	}

	while(iter->next) {
		if(iter->next == client) {
			struct client *tmp = iter->next;
			iter->next = tmp->next;
			free_client(tmp);
		} else {
			iter = iter->next;
		}
	}
}

void free_client(struct client *client)
{
	if(client) {
		free(client->name);
		free(client->strbuf.buf);
		free(client);
	}
}

int get_client_type(struct client *client)
{
	return client->type;
}

int get_client_socket(struct client *client)
{
	return client->sock;
}

#ifdef USE_X11
Window get_client_window(struct client *client)
{
	return client->win;
}
#endif

void set_client_sensitivity(struct client *client, float sens)
{
	client->sens = sens;
}

float get_client_sensitivity(struct client *client)
{
	return client->sens;
}

void set_client_device(struct client *client, struct device *dev)
{
	client->dev = dev;
}

struct device *get_client_device(struct client *client)
{
	return client->dev ? client->dev : get_devices();
}

struct client *first_client(void)
{
	client_iter = client_list;
	return client_iter;
}

struct client *next_client(void)
{
	if(client_iter) {
		client_iter = client_iter->next;
	}
	return client_iter;
}
