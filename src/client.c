#include <stdio.h>
#include <stdlib.h>
#include "client.h"

#ifdef USE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

struct client {
	int type;

	int sock;	/* UNIX domain socket */
#ifdef USE_X11
	Window win;	/* X11 client window */
#endif

	float sens;	/* sensitivity */

	struct client *next;
};


static struct client *client_list;
static struct client *citer;	/* iterator (used by first/next calls) */

int init_clients(void)
{
	if(!(client_list = malloc(sizeof *client_list))) {
		perror("failed to allocate client list");
		return -1;
	}
	client_list->next = 0;
	return 0;
}


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

	if(!(client = malloc(sizeof *client))) {
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

	client->sens = 1.0f;
	client->next = client_list->next;
	client_list->next = client;

	return client;
}

void remove_client(struct client *client)
{
	struct client *iter = client_list;

	while(iter->next) {
		if(iter->next == client) {
			struct client *tmp = iter->next;
			iter->next = tmp->next;
			free(tmp);
		} else {
			iter = iter->next;
		}
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

struct client *first_client(void)
{
	return citer = client_list->next;
}

struct client *next_client(void)
{
	citer = citer->next;
	return citer;
}
