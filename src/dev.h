#ifndef DEV_H_
#define DEV_H_

#include "event.h"

/* device hotplug detection */
int init_hotplug(void);
void shutdown_hotplug(void);
int get_hotplug_fd(void);

int handle_hotplug(void);


/* device handling */
int init_dev(void);
void shutdown_dev(void);
int get_dev_fd(void);

int read_dev(struct dev_input *inp);

void set_led(int state);

#endif	/* DEV_H_ */
