/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2025 Allin Demopolis <allindemopolis@gmail.com>

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

#if defined(__linux__) && defined(HAVE_UINPUT_H)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include "logger.h"
#include "kbemu.h"

/* keymap.c */
unsigned int keysym_to_linux_keycode(unsigned int sym);

static int uinput_fd = -1;

static void send_key_uinput(unsigned int keysym, int press);
static void send_combo_uinput(unsigned int *keys, int count, int press);
static void emit_event(int type, int code, int val);

int kbemu_uinput_init(void)
{
	int i;
	struct uinput_setup usetup;

	if(uinput_fd >= 0) {
		return 0;	/* already initialized */
	}

	/* Open uinput device */
	if((uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK)) == -1) {
		logmsg(LOG_ERR, "failed to open /dev/uinput: %s\n", strerror(errno));
		logmsg(LOG_ERR, "Make sure you have permissions to access /dev/uinput\n");
		return -1;
	}

	/* Enable key events */
	if(ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY) == -1) {
		logmsg(LOG_ERR, "UI_SET_EVBIT failed: %s\n", strerror(errno));
		goto err_close;
	}

	/* Enable all keyboard keys */
	for(i = KEY_ESC; i < KEY_MAX; i++) {
		if(ioctl(uinput_fd, UI_SET_KEYBIT, i) == -1) {
			/* Some keys may not be supported, continue anyway */
		}
	}

	/* Set up the virtual device */
	memset(&usetup, 0, sizeof usetup);
	usetup.id.bustype = BUS_VIRTUAL;
	usetup.id.vendor = 0x1234;	/* arbitrary vendor ID */
	usetup.id.product = 0x5678;	/* arbitrary product ID */
	snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "spacenavd virtual keyboard");

	if(ioctl(uinput_fd, UI_DEV_SETUP, &usetup) == -1) {
		logmsg(LOG_ERR, "UI_DEV_SETUP failed: %s\n", strerror(errno));
		goto err_close;
	}

	/* Create the device */
	if(ioctl(uinput_fd, UI_DEV_CREATE) == -1) {
		logmsg(LOG_ERR, "UI_DEV_CREATE failed: %s\n", strerror(errno));
		goto err_close;
	}

	/* register handlers
	 * don't touch keysym/keyname, since we can't do anything useful there
	 */
	kbemu_send_key = send_key_uinput;
	kbemu_send_combo = send_combo_uinput;

	logmsg(LOG_INFO, "Using uinput for keyboard emulation\n");
	return 0;

err_close:
	close(uinput_fd);
	uinput_fd = -1;
	return -1;
}

void kbemu_uinput_cleanup(void)
{
	if(uinput_fd >= 0) {
		ioctl(uinput_fd, UI_DEV_DESTROY);
		close(uinput_fd);
		uinput_fd = -1;
		logmsg(LOG_DEBUG, "uinput virtual keyboard destroyed\n");
	}
}

static void send_key_uinput(unsigned int keysym, int press)
{
	unsigned int keycode;

	if(uinput_fd < 0) {
		return;
	}

	/* Convert X11 KeySym to Linux keycode */
	keycode = keysym_to_linux_keycode(keysym);
	if(!keycode) {
		logmsg(LOG_WARNING, "failed to convert keysym %lu to Linux keycode\n", keysym);
		return;
	}

	/* Emit key event */
	emit_event(EV_KEY, keycode, press ? 1 : 0);
	emit_event(EV_SYN, SYN_REPORT, 0);
}

static void send_combo_uinput(unsigned int *keys, int count, int press)
{
	int i;

	if(uinput_fd < 0 || count <= 0) {
		return;
	}

	if(press) {
		return;
	}

	/* Send press events for all keys */
	for(i=0; i<count; i++) {
		unsigned int keycode = keysym_to_linux_keycode(keys[i]);
		if(keycode) {
			emit_event(EV_KEY, keycode, 1);
		}
	}
	emit_event(EV_SYN, SYN_REPORT, 0);

	/* Send release events in reverse order */
	for(i=0; i<count; i++) {
		unsigned int keycode = keysym_to_linux_keycode(keys[count - 1 - i]);
		if(keycode) {
			emit_event(EV_KEY, keycode, 0);
		}
	}
	emit_event(EV_SYN, SYN_REPORT, 0);
}

static void emit_event(int type, int code, int val)
{
	struct input_event ev;

	memset(&ev, 0, sizeof ev);
	ev.type = type;
	ev.code = code;
	ev.value = val;

	if(write(uinput_fd, &ev, sizeof ev) != sizeof ev) {
		logmsg(LOG_WARNING, "failed to write uinput event: %s\n", strerror(errno));
	}
}

#else
int spacenavd_kbemu_uinput_shut_up_empty_source_warning;
#endif	/* __linux__ && HAVE_UINPUT_H */
