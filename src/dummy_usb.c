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
#if !defined(__linux__) && !(defined(__APPLE__) && defined(__MACH__))

#include <stdio.h>
#include "dev.h"

static const char *message =
	"Unfortunately this version of spacenavd does not support USB devices on your "
	"platform yet. Make sure you are using the latest version of spacenavd.\n";

struct usb_device_info *find_usb_devices(int (*match)(const struct usb_device_info*))
{
	fputs(message, stderr);
	return 0;
}

void free_usb_devices_list(struct usb_device_info *list)
{
}

int open_dev_usb(struct device *dev)
{
	return -1;
}

/* the hotplug functions will also be missing on unsupported platforms */
int init_hotplug(void)
{
	return -1;
}

void shutdown_hotplug(void)
{
}

int get_hotplug_fd(void)
{
	return -1;
}

int handle_hotplug(void)
{
	return -1;
}

#else
int dummy_usb_c_avoid_stupid_compiler_warnings = 1;
#endif	/* unsupported platform */
