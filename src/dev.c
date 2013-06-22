/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2013 John Tsiombikas <nuclear@member.fsf.org>

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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "dev.h"
#include "dev_usb.h"
#include "dev_serial.h"
#include "event.h" /* remove pending events upon device removal */
#include "spnavd.h"

static struct device *add_device(void);
static struct device *dev_path_in_use(char const * dev_path);

static struct device *dev_list = NULL;

int init_devices(void)
{
	struct device *dev_cur;
	int i, device_added = 0;
	char **dev_path;

	/* try to open a serial device if specified in the config file */
	if(cfg.serial_dev[0]) {
		if(!dev_path_in_use(cfg.serial_dev)) {
			dev_cur = add_device();
			strcpy(dev_cur->path, cfg.serial_dev);
			if(open_dev_serial(dev_cur) == -1) {
				remove_device(dev_cur);
			} else {
				strcpy(dev_cur->name, "serial device");
				printf("using device: %s\n", cfg.serial_dev);
				device_added++;
			}
		}
	}

	dev_path = malloc(MAX_DEVICES * sizeof(char *));
	for(i=0; i<MAX_DEVICES; i++) {
		dev_path[i] = malloc(PATH_MAX);
	}

	find_usb_devices(dev_path, MAX_DEVICES, PATH_MAX);
	if(!dev_path[0][0] && !cfg.serial_dev[0]) {
		fprintf(stderr, "failed to find the any spaceball device files\n");
	}

	for(i=0; i<MAX_DEVICES; i++) {
		if(dev_path[i][0] == 0)
			break;
		if(dev_path_in_use(dev_path[i]) != NULL) {
			if(verbose) {
				fprintf(stderr, "already using device at: %s\n", dev_path[i]);
			}
			continue;
		}
		dev_cur = add_device();
		strcpy(dev_cur->path, dev_path[i]);
		if(open_dev_usb(dev_cur) == -1) {
			remove_device(dev_cur);
		} else {
			printf("using device: %s\n", dev_path[i]);
			device_added++;
		}
	}

	for(i=0; i<MAX_DEVICES; i++) {
		free(dev_path[i]);
	}
	free(dev_path);

	if(!device_added) {
		return -1;
	}

	return 0;
}

static struct device *add_device(void)
{
	struct device *dev;

	if(!(dev = malloc(sizeof *dev))) {
		return 0;
	}
	memset(dev, 0, sizeof *dev);

	printf("adding device.\n");

	dev->fd = -1;
	dev->next = dev_list;
	dev_list = dev;

	return dev_list;
}

void remove_device(struct device *dev)
{
	struct device dummy;
	struct device *iter;

	printf("removing device: %s\n", dev->name);

	dummy.next = dev_list;
	iter = &dummy;

	while(iter->next) {
		if(iter->next == dev) {
			iter->next = dev->next;
			break;
		}
		iter = iter->next;
	}
	dev_list = dummy.next;

	remove_dev_event(dev);

	if(dev->close) {
		dev->close(dev);
	}
	free(dev);
}

static struct device *dev_path_in_use(char const *dev_path)
{
	struct device *iter = dev_list;
	while(iter) {
		if(strcmp(iter->path, dev_path) == 0) {
			return iter;
		}
		iter = iter->next;
	}
	return 0;
}

int get_device_fd(struct device *dev)
{
	return dev ? dev->fd : -1;
}

int get_device_index(struct device *dev)
{
	struct device *iter = dev_list;
	int index = 0;
	while(iter) {
		if(dev == iter) {
			return index;
		}
		index++;
		iter = iter->next;
	}
	return -1;
}

int read_device(struct device *dev, struct dev_input *inp)
{
	if(dev->read == NULL) {
		return -1;
	}
	return dev->read(dev, inp);
}

void set_device_led(struct device *dev, int state)
{
	if(dev->set_led) {
		dev->set_led(dev, state);
	}
}

struct device *get_devices(void)
{
	return dev_list;
}
