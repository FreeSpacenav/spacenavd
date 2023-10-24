/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2023 John Tsiombikas <nuclear@member.fsf.org>

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

#ifdef USE_HIDAPI
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hidapi/hidapi.h>
#include "dev.h"
#include "dev_usb.h"
#include "spnavd.h"
#include "event.h"
#include "hotplug.h"
#include "client.h"


int open_dev_usb(struct device *dev)
{
	return -1;
}

struct usb_dev_info *find_usb_devices(int (*match)(const struct usb_dev_info*))
{
	int len;
	struct hid_device_info *hidlist, *hid;
	struct usb_dev_info *dev, *devlist = 0;

	if(!(hidlist = hid_enumerate(0, 0))) {
		return 0;
	}

	hid = hidlist;
	while(hid) {
		if(!(dev = malloc(sizeof *dev))) {
			fprintf(stderr, "failed to allocate device list node\n");
			hid = hid->next;
			continue;
		}

		len = wcslen(hid->product_string) + 1;
		if(!(dev->name = malloc(len + 1))) {
			fprintf(stderr, "failed to allocate buffer for device name\n");
			free(dev);
			hid = hid->next;
			continue;
		}
		sprintf(dev->name, "%ls", hid->product_string);

		dev->num_devfiles = 1;
		if(!(dev->devfiles[0] = strdup(hid->path))) {
			fprintf(stderr, "failed to allocate buffer for device path\n");
			free(dev->name);
			free(dev);
			hid = hid->next;
			continue;
		}

		dev->vendorid = hid->vendor_id;
		dev->productid = hid->product_id;

		/* check with the user-supplied matching callback to see if we should include
		 * this device in the returned list or not...
		 */
		if(!match || match(dev)) {
			printf("DBG %x:%x %s (%x,%x) - %s\n", dev->vendorid, dev->productid, dev->devfiles[0],
					hid->usage_page, hid->usage, dev->name);
			/*dev->next = devlist;
			devlist = dev;*/
		} else {
			free(dev->name);
			free(dev->devfiles[0]);
			free(dev);
		}

		hid = hid->next;
	}

	hid_free_enumeration(hidlist);
	return devlist;
}

#endif
