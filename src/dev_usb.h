/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2012 John Tsiombikas <nuclear@member.fsf.org>

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
#ifndef SPNAV_DEV_USB_H_
#define SPNAV_DEV_USB_H_

struct device;

int open_dev_usb(struct device *dev);

/* USB device enumeration and matching */
#define MAX_USB_DEV_FILES	16
struct usb_dev_info {
	char *name;
	int num_devfiles;
	char *devfiles[MAX_USB_DEV_FILES];
	int vendorid, productid;

	struct usb_dev_info *next;
};

struct usb_dev_info *find_usb_devices(int (*match)(const struct usb_dev_info*));
void free_usb_devices_list(struct usb_dev_info *list);
void print_usb_device_info(struct usb_dev_info *devinfo);

#endif	/* SPNAV_DEV_USB_H_ */
