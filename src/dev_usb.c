/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2019 John Tsiombikas <nuclear@member.fsf.org>

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dev_usb.h"
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include "logger.h"


void free_usb_devices_list(struct sn_usb_device_info *list)
{
	while(list) {
		int i;
		struct sn_usb_device_info *tmp = list;
		list = list->next;

		free(tmp->name);
		for(i=0; i<tmp->num_devfiles; i++) {
			free(tmp->devfiles[i]);
		}
		free(tmp);
	}
}

void print_usb_device_info(struct sn_usb_device_info *devinfo)
{
	int i, sz = 64;
	char *devname, *buf, *s;

	devname = devinfo->name ? devinfo->name : "unknown";
	sz += strlen(devname);
	for(i=0; i<devinfo->num_devfiles; i++) {
		sz += strlen(devinfo->devfiles[i]) + 1;
	}

	s = buf = alloca(sz);
	s += sprintf(s, "[%x:%x]: \"%s\" (", devinfo->vendorid, devinfo->productid,
			devinfo->name ? devinfo->name : "unknown");
	for(i=0; i<devinfo->num_devfiles; i++) {
		s += sprintf(s, "%s ", devinfo->devfiles[i]);
	}
	sprintf(s, ")\n");

	logmsg(LOG_INFO, buf);
}
