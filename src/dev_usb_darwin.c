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
#if defined(__APPLE__) && defined(__MACH__)

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDLib.h>
#include "spnavd.h"
#include "dev.h"
#include "dev_usb.h"

int open_dev_usb(struct device *dev)
{
	return -1;
}

struct usb_dev_info *find_usb_devices(int (*match)(const struct usb_dev_info*))
{
	struct usb_dev_info *devlist = 0;
	struct usb_dev_info devinfo;
	/*static const int vendor_id = 1133;*/	/* 3dconnexion */
	static char dev_path[512];
	io_object_t dev;
	io_iterator_t iter;
	CFMutableDictionaryRef match_dict;
	CFNumberRef number_ref;

	match_dict = IOServiceMatching(kIOHIDDeviceKey);

	/* add filter rule: vendor-id should be 3dconnexion's */
	/*number_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor_id);
	CFDictionarySetValue(match_dict, CFSTR(kIOHIDVendorIDKey), number_ref);
	CFRelease(number_ref);
	*/

	/* fetch... */
	if(IOServiceGetMatchingServices(kIOMasterPortDefault, match_dict, &iter) != kIOReturnSuccess) {
		logmsg(LOG_ERR, "failed to retrieve USB HID devices\n");
		return 0;
	}

	while((dev = IOIteratorNext(iter))) {
		memset(&devinfo, 0, sizeof devinfo);

		IORegistryEntryGetPath(dev, kIOServicePlane, dev_path);
		if(!(devinfo.devfiles[0] = strdup(dev_path))) {
			logmsg(LOG_ERR, "failed to allocate device file path buffer: %s\n", strerror(errno));
			continue;
		}
		devinfo.num_devfiles = 1;

		/* TODO retrieve vendor id and product id */

		if(!match || match(&devinfo)) {
			struct usb_dev_info *node = malloc(sizeof *node);
			if(node) {
				if(verbose) {
					logmsg(LOG_INFO, "found usb device: ");
					print_usb_device_info(&devinfo);
				}

				*node = devinfo;
				node->next = devlist;
				devlist = node;
			} else {
				free(devinfo.devfiles[0]);
				logmsg(LOG_ERR, "failed to allocate usb device info node: %s\n", strerror(errno));
			}
		}
	}

	IOObjectRelease(dev);
	IOObjectRelease(iter);
	return devlist;
}

#else
int spacenavd_dev_usb_darwin_silence_empty_warning;
#endif	/* __APPLE__ && __MACH__ */
