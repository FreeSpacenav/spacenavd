/* SpaceMouse Enterprise backlight control through the Linux HID driver.
 * GPLv3-or-later, like spacenavd.
 * Protocol reference: TheHoodedFoot/SpaceLCD doc/notes.md and
 * res/dat/lcdoff.control: feature report 0x11, brightness 0..100.
 */
#include "config.h"
#include "lcd.h"
#if defined(HAVE_SPACELCD) && defined(__linux__)
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include "logger.h"

#ifndef LCD_SYS_HIDRAW_DIR
#define LCD_SYS_HIDRAW_DIR "/sys/class/hidraw"
#endif
#ifndef LCD_DEV_DIR
#define LCD_DEV_DIR "/dev"
#endif

static int read_usb_number(const char *device, const char *attribute)
{
	char path[PATH_MAX];
	FILE *fp;
	int value = -1, len;
	len = snprintf(path, sizeof path, "%s/device/../../%s", device, attribute);
	if(len < 0 || (size_t)len >= sizeof path) return -1;
	if(!(fp = fopen(path, "r"))) return -1;
	if(fscanf(fp, "%d", &value) != 1) value = -1;
	fclose(fp);
	return value;
}

int lcd_hid_set_brightness(int bus, int address, int level)
{
	glob_t paths;
	size_t i;
	int fd, len, result = -1;
	char path[PATH_MAX];
	struct hidraw_devinfo info;
	unsigned char report[2];

	if(level < 0 || level > 100) return -1;
	memset(&paths, 0, sizeof paths);
	if(glob(LCD_SYS_HIDRAW_DIR "/hidraw*", 0, 0, &paths)) {
		globfree(&paths);
		return -1;
	}
	for(i = 0; i < paths.gl_pathc; i++) {
		const char *entry = paths.gl_pathv[i];
		const char *name = strrchr(entry, '/');
		/* Match the same physical device used for the bulk image upload. */
		if(read_usb_number(entry, "busnum") != bus || read_usb_number(entry, "devnum") != address) continue;
		if(!name) continue;
		len = snprintf(path, sizeof path, LCD_DEV_DIR "/%s", name + 1);
		if(len < 0 || (size_t)len >= sizeof path) continue;
		if((fd = open(path, O_RDWR | O_CLOEXEC)) < 0) continue;
		memset(&info, 0, sizeof info);
		if(ioctl(fd, HIDIOCGRAWINFO, &info) >= 0 && info.bustype == BUS_USB &&
				(unsigned short)info.vendor == 0x256f && (unsigned short)info.product == 0xc633) {
			report[0] = 0x11;
			report[1] = level;
			/* Feature reports use the control endpoint without detaching HID. */
			if(ioctl(fd, HIDIOCSFEATURE(sizeof report), report) == sizeof report) result = 0;
			close(fd);
			break;
		}
		close(fd);
	}
	globfree(&paths);
	if(result < 0) logmsg(LOG_WARNING, "lcd: failed to set backlight (check hidraw permissions)\n");
	return result;
}
#else
int lcd_hid_set_brightness(int bus, int address, int level)
{
	(void)bus; (void)address; (void)level;
	return -1;
}
#endif
