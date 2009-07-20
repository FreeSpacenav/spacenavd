#ifndef CLIENT_H_
#define CLIENT_H_

#ifdef USE_X11
#include <X11/Xlib.h>
#endif

/* client types */
enum {
	CLIENT_X11,		/* through the magellan X11 protocol */
	CLIENT_UNIX		/* through the new UNIX domain socket */
};


struct client;

int init_clients(void);

struct client *add_client(int type, void *cdata);
void remove_client(struct client *client);

int get_client_type(struct client *client);
int get_client_socket(struct client *client);
#ifdef USE_X11
Window get_client_window(struct client *client);
#endif

void set_client_sensitivity(struct client *client, float sens);
float get_client_sensitivity(struct client *client);

/* these two can be used to iterate over all clients */
struct client *first_client(void);
struct client *next_client(void);


#endif	/* CLIENT_H_ */
