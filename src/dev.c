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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "dev.h"
#include "dev_usb.h"
#include "dev_serial.h"
#include "event.h" /* remove pending events upon device removal */
#include "spnavd.h"
#include "proto.h"
#include "proto_unix.h"

#ifdef USE_X11
#include "proto_x11.h"
#endif

static struct device *add_device(void);
static struct device *dev_path_in_use(char const * dev_path);
static int match_usbdev(const struct usb_dev_info *devinfo);
static int usbdevtype(unsigned int vid, unsigned int pid);

static struct device *dev_list = NULL;
static unsigned short last_id;

int init_devices(void)
{
	init_devices_serial();
	return init_devices_usb();
}

int init_devices_serial(void)
{
	struct device *dev;
	spnav_event ev = {0};

	/* try to open a serial device if specified in the config file */
	if(cfg.serial_dev[0]) {
		if(!dev_path_in_use(cfg.serial_dev)) {
			dev = add_device();
			strcpy(dev->path, cfg.serial_dev);
			if(open_dev_serial(dev) == -1) {
				remove_device(dev);
			} else {
				strcpy(dev->name, "serial device");
				logmsg(LOG_INFO, "using device: %s\n", cfg.serial_dev);
				device_added++;
			}

			/* new serial device added, send device change event */
			ev.dev.type = EVENT_DEV;
			ev.dev.op = DEV_ADD;
			ev.dev.id = dev->id;
			ev.dev.devtype = dev->type;
			broadcast_event(&ev);
		}
	}

	return 0;
}


int init_devices_usb(void)
{
	struct device *dev;
	int i, device_added = 0;
	struct usb_dev_info *usblist, *usbdev;
	spnav_event ev = {0};

	/* detect any supported USB devices */
	usblist = find_usb_devices(match_usbdev);

	usbdev = usblist;
	while(usbdev) {
		for(i=0; i<usbdev->num_devfiles; i++) {
			if((dev = dev_path_in_use(usbdev->devfiles[i]))) {
				if(verbose) {
					logmsg(LOG_WARNING, "already using device: %s (%s) (id: %d)\n", dev->name, dev->path, dev->id);
				}
				break;
			}

			dev = add_device();
			strcpy(dev->path, usbdev->devfiles[i]);
			dev->type = usbdevtype(usbdev->vendorid, usbdev->productid);
			dev->usbid[0] = usbdev->vendorid;
			dev->usbid[1] = usbdev->productid;

			if(open_dev_usb(dev) == -1) {
				remove_device(dev);
			} else {
				logmsg(LOG_INFO, "using device: %s (%s)\n", dev->name, dev->path);
				device_added++;

				/* new USB device added, send device change event */
				ev.dev.type = EVENT_DEV;
				ev.dev.op = DEV_ADD;
				ev.dev.id = dev->id;
				ev.dev.devtype = dev->type;
				ev.dev.usbid[0] = dev->usbid[0];
				ev.dev.usbid[1] = dev->usbid[1];
				broadcast_event(&ev);
				break;
			}
		}
		usbdev = usbdev->next;
	}

	free_usb_devices_list(usblist);

	if(!usblist) {
		logmsg(LOG_ERR, "failed to find any supported USB devices\n");
		return -1;
	}

#ifdef USE_X11
	drop_xinput();
#endif
	return 0;
}

static struct device *add_device(void)
{
	struct device *dev;

	if(!(dev = malloc(sizeof *dev))) {
		return 0;
	}
	memset(dev, 0, sizeof *dev);

	dev->fd = -1;
	dev->id = last_id++;
	dev->next = dev_list;
	dev_list = dev;

	logmsg(LOG_INFO, "adding device (id: %d).\n", dev->id);

	return dev_list;
}

void remove_device(struct device *dev)
{
	struct device dummy;
	struct device *iter;
	spnav_event ev;

	logmsg(LOG_INFO, "removing device: %s (id: %d)\n", dev->name, dev->id);

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

	/* send device change event to clients */
	ev.dev.type = EVENT_DEV;
	ev.dev.op = DEV_RM;
	ev.dev.id = dev->id;
	ev.dev.devtype = dev->type;
	ev.dev.usbid[0] = dev->usbid[0];
	ev.dev.usbid[1] = dev->usbid[1];
	broadcast_event(&ev);

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

void set_devices_led(int state)
{
	struct device *dev = get_devices();
	while(dev) {
		set_device_led(dev, state);
		dev = dev->next;
	}
}

struct device *get_devices(void)
{
	return dev_list;
}

#define VENDOR_3DCONNEXION	0x256f

static int devid_list[][3] = {
	{0x046d, 0xc603, DEV_PLUSXT},		/* spacemouse plus XT */
	{0x046d, 0xc605, DEV_CADMAN},		/* cadman */
	{0x046d, 0xc606, DEV_SMCLASSIC},	/* spacemouse classic */
	{0x046d, 0xc621, DEV_SB5000},		/* spaceball 5000 */
	{0x046d, 0xc623, DEV_STRAVEL},		/* space traveller */
	{0x046d, 0xc625, DEV_SPILOT},		/* space pilot */
	{0x046d, 0xc626, DEV_SNAV},			/* space navigator */
	{0x046d, 0xc627, DEV_SEXP},			/* space explorer */
	{0x046d, 0xc628, DEV_SNAVNB},		/* space navigator for notebooks*/
	{0x046d, 0xc629, DEV_SPILOTPRO},	/* space pilot pro*/
	{0x046d, 0xc62b, DEV_SMPRO},		/* space mouse pro*/
	{0x046d, 0xc640, DEV_NULOOQ},		/* nulooq */
	{0x256f, 0xc62e, DEV_SMW},			/* spacemouse wireless (USB cable) */
	{0x256f, 0xc62f, DEV_SMW},			/* spacemouse wireless  receiver */
	{0x256f, 0xc631, DEV_SMPROW},		/* spacemouse pro wireless */
	{0x256f, 0xc632, DEV_SMPROW},		/* spacemouse pro wireless receiver */
	{0x256f, 0xc633, DEV_SMENT},		/* spacemouse enterprise */
	{0x256f, 0xc635, DEV_SMCOMP},		/* spacemouse compact */
	{0x256f, 0xc636, DEV_SMMOD},		/* spacemouse module */

	{-1, -1, DEV_UNKNOWN}
};

/* 3Dconnexion devices which we don't want to match, because they are
 * not 6dof space-mice. reported by: Herbert Graeber in github pull request #4
 */
static int devid_blacklist[][2] = {
	{0x256f, 0xc650},	/* cadmouse */
	{0x256f, 0xc651},	/* cadmouse wireless */
	{0x256f, 0xc62c},	/* lipari(?) */
	{0x256f, 0xc641},	/* scout(?) */

	{-1, -1}
};


static int match_usbdev(const struct usb_dev_info *devinfo)
{
	int i;

	/* match any USB devices listed in the config file */
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

	if(devinfo->vendorid != -1 && devinfo->productid != -1) {
		int vid = devinfo->vendorid;
		int pid = devinfo->productid;

		/* ignore any device in the devid_blacklist */
		for(i=0; devid_blacklist[i][0] > 0; i++) {
			if(vid == devid_blacklist[i][0] && pid == devid_blacklist[i][1]) {
				return 0;
			}
		}

		/* match any device with the new 3Dconnexion device id */
		if(vid == VENDOR_3DCONNEXION) {
			/* avoid matching and trying to grab the CAD mouse, when connected
			 * on the same universal receiver as the spacemouse.
			 */
			if(pid == 0xc652 && strstr(devinfo->name, "Universal Receiver Mouse")) {
				return 0;
			}
			return 1;
		}

		/* match any device in the devid_list */
		for(i=0; devid_list[i][0] > 0; i++) {
			if(vid == devid_list[i][0] && pid == devid_list[i][1]) {
				return 1;
			}
		}
	}

	/* if it's a 3Dconnexion device match it immediately */
	if((devinfo->name && strstr(devinfo->name, "3Dconnexion"))) {
		return 1;
	}

	return 0;	/* no match */
}

static int usbdevtype(unsigned int vid, unsigned int pid)
{
	int i;
	for(i=0; devid_list[i][0] != -1; i++) {
		if(devid_list[i][0] == vid && devid_list[i][1] == pid) {
			return devid_list[i][2];
		}
	}
	return DEV_UNKNOWN;
}
