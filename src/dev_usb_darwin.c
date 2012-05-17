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
#if defined(__APPLE__) && defined(__MACH__)

#include "config.h"
#include <stdio.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDLib.h>
#include "dev.h"

int open_dev_usb(struct device *dev, const char *path)
{
	return -1;
}

const char *find_usb_device(void)
{
	static const int vendor_id = 1133;	/* 3dconnexion */
	static char dev_path[512];
	io_object_t dev;
	io_iterator_t iter;
	CFMutableDictionaryRef match_dict;
	CFNumberRef number_ref;

	match_dict = IOServiceMatching(kIOHIDDeviceKey);

	/* add filter rule: vendor-id should be 3dconnexion's */
	number_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor_id);
	CFDictionarySetValue(match_dict, CFSTR(kIOHIDVendorIDKey), number_ref);
	CFRelease(number_ref);

	/* fetch... */
	if(IOServiceGetMatchingServices(kIOMasterPortDefault, match_dict, &iter) != kIOReturnSuccess) {
		fprintf(stderr, "failed to retrieve USB HID devices\n");
		return 0;
	}

	dev = IOIteratorNext(iter);

	IORegistryEntryGetPath(dev, kIOServicePlane, dev_path);

	IOObjectRelease(dev);
	IOObjectRelease(iter);
	return dev_path;
}

#endif	/* __APPLE__ && __MACH__ */
