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
#include "proto_x11.h"

static struct device *add_device(void);
static struct device *dev_path_in_use(char const * dev_path);
static int match_usbdev(const struct usb_device_info *devinfo);

static struct device *dev_list = NULL;

int init_devices(void)
{
	struct device *dev;
	int i, device_added = 0;
	struct usb_device_info *usblist, *usbdev;

	/* try to open a serial device if specified in the config file */
	if(cfg.serial_dev[0]) {
		if(!dev_path_in_use(cfg.serial_dev)) {
			dev = add_device();
			strcpy(dev->path, cfg.serial_dev);
			if(open_dev_serial(dev) == -1) {
				remove_device(dev);
			} else {
				strcpy(dev->name, "serial device");
				printf("using device: %s\n", cfg.serial_dev);
				device_added++;
			}
		}
	}

	/* detect any supported USB devices */
	usblist = find_usb_devices(match_usbdev);

	usbdev = usblist;
	while(usbdev) {
		for(i=0; i<usbdev->num_devfiles; i++) {
			if((dev = dev_path_in_use(usbdev->devfiles[i]))) {
				if(verbose) {
					fprintf(stderr, "already using device: %s (%s)\n", dev->name, dev->path);
				}
				break;
			}

			dev = add_device();
			strcpy(dev->path, usbdev->devfiles[i]);

			if(open_dev_usb(dev) == -1) {
				remove_device(dev);
			} else {
				printf("using device: %s\n", dev->path);
				device_added++;
				break;
			}
		}
		usbdev = usbdev->next;
	}

	free_usb_devices_list(usblist);

	if(!device_added) {
		fprintf(stderr, "failed to find any supported devices\n");
		return -1;
	}

	drop_xinput();
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

#define VENDOR_3DCONNEXION	0x256f

static int devid_list[][2] = {
	/* 3Dconnexion devices */
	{0x46d, 0xc603},	/* spacemouse plus XT */
	{0x46d, 0xc605},	/* cadman */
	{0x46d, 0xc606},	/* spacemouse classic */
	{0x46d, 0xc621},	/* spaceball 5000 */
	{0x46d, 0xc623},	/* space traveller */
	{0x46d, 0xc625},	/* space pilot */
	{0x46d, 0xc626},	/* space navigator */
	{0x46d, 0xc627},	/* space explorer */
	{0x46d, 0xc628},	/* space navigator for notebooks*/
	{0x46d, 0xc629},	/* space pilot pro*/
	{0x46d, 0xc62b},	/* space mouse pro*/
	{0x46d, 0xc640},	/* nulooq */

	{-1, -1}
};

static int match_usbdev(const struct usb_device_info *devinfo)
{
	int i;

	/* if it's a 3Dconnexion device match it immediately */
	if((devinfo->name && strstr(devinfo->name, "3Dconnexion"))) {
		return 1;
	}

	if(devinfo->vendorid != -1 && devinfo->productid != -1) {
		/* match any device with the new 3Dconnexion device id */
		if(devinfo->vendorid == VENDOR_3DCONNEXION) {
			return 1;
		}

		/* match any device in the devid_list */
		for(i=0; devid_list[i][0] > 0; i++) {
			if(devinfo->vendorid == devid_list[i][0] && devinfo->productid == devid_list[i][1]) {
				return 1;
			}
		}
	}

	/* match any joystick devices listed in the config file */
	for(i=0; i<MAX_CUSTOM; i++) {
		if(cfg.devid[i][0] != -1 && cfg.devid[i][1] != -1 &&
				(unsigned int)cfg.devid[i][0] == devinfo->vendorid &&
				(unsigned int)cfg.devid[i][1] == devinfo->productid) {
			return 1;
		}
		if(cfg.devname[i] && devinfo->name && strcmp(cfg.devname[i], devinfo->name) == 0) {
			return 1;
		}
	}

	return 0;	/* no match */
}
