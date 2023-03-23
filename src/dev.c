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
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
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


/* The device flags are introduced to normalize input across all known
 * supported 6dof devices. Newer USB devices seem to use axis 1 as fwd/back and
 * axis 2 as up/down, while older serial devices (and possibly also the early
 * USB ones?) do the opposite. This discrepancy would mean the user has to
 * change the configuration back and forth when changing devices. With these
 * flags we attempt to make all known devices use the same input axes at the
 * lowest level, and let the user remap based on preference, and have their
 * choice persist across all known devices.
 */
enum {
	DF_SWAPYZ = 1,
	DF_INVYZ = 2
};

/* The bnmap function pointer in the device table was introduced to deal with
 * certain USB devices which report a huge amount of buttons, and strange
 * disjointed button numbers. The function is expected to return the number of
 * actual buttons when a negative number is passed as argument, and the button
 * mapping otherwise.
 */
static struct usbdb_entry {
	int usbid[2];
	int type;
	unsigned int flags;
	int (*bnmap)(int);		/* remap buttons on problematic devices */
} usbdb[] = {
	{{0x046d, 0xc603}, DEV_PLUSXT,		0,						0},				/* spacemouse plus XT */
	{{0x046d, 0xc605}, DEV_CADMAN,		0,						0},				/* cadman */
	{{0x046d, 0xc606}, DEV_SMCLASSIC,	0,						0},				/* spacemouse classic */
	{{0x046d, 0xc621}, DEV_SB5000,		0,						0},				/* spaceball 5000 */
	{{0x046d, 0xc623}, DEV_STRAVEL,		DF_SWAPYZ | DF_INVYZ,	0},				/* space traveller */
	{{0x046d, 0xc625}, DEV_SPILOT,		DF_SWAPYZ | DF_INVYZ,	0},				/* space pilot */
	{{0x046d, 0xc626}, DEV_SNAV,		DF_SWAPYZ | DF_INVYZ,	0},				/* space navigator */
	{{0x046d, 0xc627}, DEV_SEXP,		DF_SWAPYZ | DF_INVYZ,	0},				/* space explorer */
	{{0x046d, 0xc628}, DEV_SNAVNB,		DF_SWAPYZ | DF_INVYZ,	0},				/* space navigator for notebooks*/
	{{0x046d, 0xc629}, DEV_SPILOTPRO,	DF_SWAPYZ | DF_INVYZ,	0},				/* space pilot pro*/
	{{0x046d, 0xc62b}, DEV_SMPRO,		DF_SWAPYZ | DF_INVYZ,	bnhack_smpro},	/* space mouse pro*/
	{{0x046d, 0xc640}, DEV_NULOOQ,		0,						0},				/* nulooq */
	{{0x256f, 0xc62e}, DEV_SMW,			DF_SWAPYZ | DF_INVYZ,	0},				/* spacemouse wireless (USB cable) */
	{{0x256f, 0xc62f}, DEV_SMW,			DF_SWAPYZ | DF_INVYZ,	0},				/* spacemouse wireless  receiver */
	{{0x256f, 0xc631}, DEV_SMPROW,		DF_SWAPYZ | DF_INVYZ,	bnhack_smpro},	/* spacemouse pro wireless */
	{{0x256f, 0xc632}, DEV_SMPROW,		DF_SWAPYZ | DF_INVYZ,	bnhack_smpro},	/* spacemouse pro wireless receiver */
	{{0x256f, 0xc633}, DEV_SMENT,		DF_SWAPYZ | DF_INVYZ,	bnhack_sment},	/* spacemouse enterprise */
	{{0x256f, 0xc635}, DEV_SMCOMP,		DF_SWAPYZ | DF_INVYZ,	0},				/* spacemouse compact */
	{{0x256f, 0xc636}, DEV_SMMOD,		DF_SWAPYZ | DF_INVYZ,	0},				/* spacemouse module */

	{{-1, -1}, DEV_UNKNOWN, 0}
};

/* 3Dconnexion devices which we don't want to match, because they are
 * not 6dof space-mice. reported by: Herbert Graeber in github pull request #4
 */
static int devid_blacklist[][2] = {
	{0x256f, 0xc650},	/* cadmouse */
	{0x256f, 0xc651},	/* cadmouse wireless */
	{0x256f, 0xc654},	/* CadMouse Pro Wireless */
	{0x256f, 0xc655},	/* CadMouse Compact */
	{0x256f, 0xc656},	/* CadMouse Pro */
	{0x256f, 0xc657},	/* CadMouse Pro Wireless Left */
	{0x256f, 0xc658},	/* CadMouse Compact Wireless */
	{0x256f, 0xc62c},	/* lipari(?) */
	{0x256f, 0xc641},	/* scout(?) */

	{-1, -1}
};



static struct device *add_device(void);
static int match_usbdev(const struct usb_dev_info *devinfo);
static struct usbdb_entry *find_usbdb_entry(unsigned int vid, unsigned int pid);

static struct device *dev_list = NULL;
static unsigned short last_id;

void init_devices(void)
{
	init_devices_serial();
	init_devices_usb();
}

void init_devices_serial(void)
{
	struct stat st;
	struct device *dev;
	spnav_event ev = {0};

	/* try to open a serial device if specified in the config file */
	if(cfg.serial_dev[0]) {
		if(!dev_path_in_use(cfg.serial_dev)) {
			if(stat(cfg.serial_dev, &st) == -1) {
				logmsg(LOG_ERR, "Failed to stat serial device %s: %s\n",
						cfg.serial_dev, strerror(errno));
				return;
			}
			if(!S_ISCHR(st.st_mode)) {
				logmsg(LOG_ERR, "Ignoring configured serial device: %s: %s\n",
						cfg.serial_dev, "not a character device");
				return;
			}

			dev = add_device();
			strcpy(dev->path, cfg.serial_dev);
			if(open_dev_serial(dev) == -1) {
				remove_device(dev);
				return;
			}
			logmsg(LOG_INFO, "using device: %s\n", cfg.serial_dev);

			/* new serial device added, send device change event */
			ev.dev.type = EVENT_DEV;
			ev.dev.op = DEV_ADD;
			ev.dev.id = dev->id;
			ev.dev.devtype = dev->type;
			broadcast_event(&ev);
		}
	}
}


int init_devices_usb(void)
{
	int i;
	struct device *dev;
	struct usb_dev_info *usblist, *usbdev;
	struct usbdb_entry *uent;
	spnav_event ev = {0};
	char buf[256];

	/* detect any supported USB devices */
	usblist = find_usb_devices(match_usbdev);

	usbdev = usblist;
	while(usbdev) {
		for(i=0; i<usbdev->num_devfiles; i++) {
			if((dev = dev_path_in_use(usbdev->devfiles[i]))) {
				if(verbose > 1) {
					logmsg(LOG_WARNING, "already using device: %s (%s) (id: %d)\n", dev->name, dev->path, dev->id);
				}
				break;
			}

			uent = find_usbdb_entry(usbdev->vendorid, usbdev->productid);

			dev = add_device();
			strcpy(dev->path, usbdev->devfiles[i]);
			dev->type = uent ? uent->type : DEV_UNKNOWN;
			dev->flags = uent ? uent->flags : 0;
			dev->bnhack = uent ? uent->bnmap : 0;
			dev->usbid[0] = usbdev->vendorid;
			dev->usbid[1] = usbdev->productid;

			if(open_dev_usb(dev) == -1) {
				remove_device(dev);
			} else {
				/* add the 6dof remapping flags to every future 3dconnexion device */
				if(dev->usbid[0] == VID_3DCONN) {
					dev->flags |= DF_SWAPYZ | DF_INVYZ;
				}
				/* sanity-check the device flags */
				if((dev->flags & (DF_SWAPYZ | DF_INVYZ)) && dev->num_axes != 6) {
					logmsg(LOG_WARNING, "BUG: Tried to add 6dof device flags to a device with %d axes. Please report this as a bug\n", dev->num_axes);
					dev->flags &= ~(DF_SWAPYZ | DF_INVYZ);
				}
				logmsg(LOG_INFO, "using device: %s (%s)\n", dev->name, dev->path);
				if(dev->flags) {
					strcpy(buf, "  device flags:");
					if(dev->flags & DF_SWAPYZ) strcat(buf, " swap y-z");
					if(dev->flags & DF_INVYZ) strcat(buf, " invert y-z");
					logmsg(LOG_INFO, "%s\n", buf);
				}

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
		if(verbose > 1) {
			logmsg(LOG_ERR, "failed to find any supported USB devices\n");
		}
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

	logmsg(LOG_INFO, "removing device: %s (id: %d path: %s)\n", dev->name, dev->id, dev->path);

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

struct device *dev_path_in_use(const char *dev_path)
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

	if(dev->read(dev, inp) == -1) {
		return -1;
	}

	if(inp->type == INP_MOTION) {
		if(dev->flags & DF_SWAPYZ) {
			static const int swap[] = {0, 2, 1, 3, 5, 4};
			inp->idx = swap[inp->idx];
		}
		if((dev->flags & DF_INVYZ) && inp->idx != 0 && inp->idx != 3) {
			inp->val = -inp->val;
		}
	}
	return 0;
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
		if(vid == VID_3DCONN) {
			/* avoid matching and trying to grab the CAD mouse, when connected
			 * on the same universal receiver as the spacemouse.
			 */
			if(pid == 0xc652 && strstr(devinfo->name, "Universal Receiver Mouse")) {
				return 0;
			}
			return 1;
		}

		/* match any device in the usbdb */
		for(i=0; usbdb[i].usbid[0] > 0; i++) {
			if(vid == usbdb[i].usbid[0] && pid == usbdb[i].usbid[1]) {
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

static struct usbdb_entry *find_usbdb_entry(unsigned int vid, unsigned int pid)
{
	int i;
	for(i=0; usbdb[i].usbid[0] != -1; i++) {
		if(usbdb[i].usbid[0] == vid && usbdb[i].usbid[1] == pid) {
			return usbdb + i;
		}
	}
	return 0;
}


/* --- button remapping hack functions --- */

/* SpaceMouse Pro */
int bnhack_smpro(int bn)
{
	if(bn < 0) return 15;	/* button count */

	switch(bn) {
	case 256: return 4;		/* menu */
	case 257: return 5;		/* fit */
	case 258: return 6;		/* [T] */
	case 260: return 7;		/* [R] */
	case 261: return 8;		/* [F] */
	case 264: return 9;		/* [ ] */
	case 268: return 0;		/* 1 */
	case 269: return 1;		/* 2 */
	case 270: return 2;		/* 3 */
	case 271: return 3;		/* 4 */
	case 278: return 11;	/* esc */
	case 279: return 12;	/* alt */
	case 280: return 13;	/* shift */
	case 281: return 14;	/* ctrl */
	case 282: return 10;	/* lock */
	default:
		break;
	}
	return -1;	/* ignore all other events */
}

/* SpaceMouse Enterprise */
int bnhack_sment(int bn)
{
	if(bn < 0) return 31;	/* button count */

	switch(bn) {
	case 256: return 12;	/* menu */
	case 257: return 13;	/* fit */
	case 258: return 14;	/* [T] */
	case 260: return 15;	/* [R] */
	case 261: return 16;	/* [F] */
	case 264: return 17;	/* [ ] */

	case 266: return 30;	/* iso */

	case 268: return 0;		/* 1 */
	case 269: return 1;		/* 2 */
	case 270: return 2;		/* 3 */
	case 271: return 3;		/* 4 */
	case 272: return 4;		/* 5 */
	case 273: return 5;		/* 6 */
	case 274: return 6;		/* 7 */
	case 275: return 7;		/* 8 */
	case 276: return 8;		/* 9 */
	case 277: return 9;		/* 10 */

	case 278: return 18;	/* esc */
	case 279: return 19;	/* alt */
	case 280: return 20;	/* shift */
	case 281: return 21;	/* ctrl */
	case 282: return 22;	/* lock */

	case 291: return 23;	/* enter */
	case 292: return 24;	/* delete */

	case 332: return 10;	/* 11 */
	case 333: return 11;	/* 12 */

	case 358: return 27;	/* V1 */
	case 359: return 28;	/* V2 */
	case 360: return 29;	/* V3 */

	case 430: return 25;	/* tab */
	case 431: return 26;	/* space */
	default:
		break;
	}
	return -1;	/* ignore all other events */
}
