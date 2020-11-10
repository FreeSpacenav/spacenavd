/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2020 John Tsiombikas <nuclear@member.fsf.org>

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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include "dev_serial.h"
#include "dev.h"
#include "event.h"

#if  defined(__i386__) || defined(__ia64__) || defined(WIN32) || \
    (defined(__alpha__) || defined(__alpha)) || \
     defined(__arm__) || \
    (defined(__mips__) && defined(__MIPSEL__)) || \
     defined(__SYMBIAN32__) || \
     defined(__x86_64__) || \
     defined(__LITTLE_ENDIAN__)
#define SBALL_LITTLE_ENDIAN
#else
#define SBALL_BIG_ENDIAN
#endif

#define INP_BUF_SZ	256

#define EVQUEUE_SZ	64

enum {
	SB4000	= 1,
	FLIPXY	= 2
};

struct sball {
	int fd;
	unsigned int flags;
	int nbuttons;

	char buf[INP_BUF_SZ];
	int len;

	short mot[6];
	unsigned int keystate, keymask;

	struct termios saved_term;
	int saved_mstat;

	struct dev_input evqueue[EVQUEUE_SZ];
	int evq_rd, evq_wr;

	int (*parse)(struct sball*, int, char*, int);
};


static void close_dev_serial(struct device *dev);
static int read_dev_serial(struct device *dev, struct dev_input *inp);

static int stty_sball(struct sball *sb);
static int stty_mag(struct sball *sb);
static void stty_save(struct sball *sb);
static void stty_restore(struct sball *sb);

static int proc_input(struct sball *sb);

static int mag_parsepkt(struct sball *sb, int id, char *data, int len);
static int sball_parsepkt(struct sball *sb, int id, char *data, int len);

static int guess_num_buttons(const char *verstr);

static void make_printable(char *buf, int len);
static int read_timeout(int fd, char *buf, int bufsz, long tm_usec);

static void enqueue_motion(struct sball *sb, int axis, int val);
static void gen_button_events(struct sball *sb, unsigned int prev);


int open_dev_serial(struct device *dev)
{
	int fd, sz;
	char buf[128];
	struct sball *sb = 0;

	if((fd = open(dev->path, O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1) {
		fprintf(stderr, "sball_open: failed to open device: %s: %s\n", dev->path, strerror(errno));
		return 0;
	}

	if(!(sb = calloc(1, sizeof *sb))) {
		fprintf(stderr, "sball_open: failed to allocate sball object\n");
		goto err;
	}
	dev->data = sb;
	dev->fd = sb->fd = fd;
	dev->close = close_dev_serial;
	dev->read = read_dev_serial;

	stty_save(sb);

	if(stty_sball(sb) == -1) {
		goto err;
	}
	write(fd, "\r@RESET\r", 8);

	if((sz = read_timeout(fd, buf, sizeof buf - 1, 2000000)) > 0 && (buf[sz] = 0, strstr(buf, "\r@1"))) {
		/* we got a response, so it's a spaceball */
		make_printable(buf, sz);
		printf("Spaceball detected: %s\n", buf);

		sb->nbuttons = guess_num_buttons(buf);
		sb->keymask = 0xffff >> (16 - sb->nbuttons);
		printf("%d buttons\n", sb->nbuttons);

		/* set binary mode and enable automatic data packet sending. also request
		 * a key event to find out as soon as possible if this is a 4000flx with
		 * 12 buttons
		*/
		write(fd, "\rCB\rMSSV\rk\r", 11);

		sb->parse = sball_parsepkt;
		return 0;
	}

	/* try as a magellan spacemouse */
	if(stty_mag(sb) == -1) {
		goto err;
	}
	write(fd, "vQ\r", 3);

	if((sz = read_timeout(fd, buf, sizeof buf - 1, 250000)) > 0 && buf[0] == 'v') {
		make_printable(buf, sz);
		printf("Magellan SpaceMouse detected:\n%s\n", buf);

		sb->nbuttons = guess_num_buttons(buf);
		sb->keymask = 0xffff >> (16 - sb->nbuttons);
		printf("%d buttons\n", sb->nbuttons);

		/* set 3D mode, not-dominant-axis, pass through motion and button packets */
		write(fd, "m3\r", 3);

		sb->parse = mag_parsepkt;
		return 0;
	}

err:
	stty_restore(sb);
	close(fd);
	free(sb);
	return -1;
}

static void close_dev_serial(struct device *dev)
{
	if(dev->data) {
		stty_restore(dev->data);
		close(dev->fd);
	}
	dev->data = 0;
}

static int read_dev_serial(struct device *dev, struct dev_input *inp)
{
	int sz;
	struct sball *sb = dev->data;

	if(!sb) return -1;

	while((sz = read(sb->fd, sb->buf + sb->len,  INP_BUF_SZ - sb->len - 1)) > 0) {
		sb->len += sz;
		proc_input(sb);
	}

	/* if we fill the input buffer, make a last attempt to parse it, and discard
	 * it so we can receive more
	 */
	if(sb->len >= INP_BUF_SZ) {
		proc_input(sb);
		sb->len = 0;
	}

	if(sb->evq_rd != sb->evq_wr) {
		*inp = sb->evqueue[sb->evq_rd];
		sb->evq_rd = (sb->evq_rd + 1) & (EVQUEUE_SZ - 1);
		return 0;
	}
	return -1;
}

/* Labtec spaceball: 9600 8n1 XON/XOFF
 * Can't use canonical mode to assemble input into lines for the spaceball,
 * because binary data received for motion events can include newlines which
 * would be eaten up by the line discipline. Therefore we'll rely on VTIME=1 to
 * hopefully get more than 1 byte at a time. Alternatively we could request
 * printable reports, but I don't feel like implementing that.
 */
static int stty_sball(struct sball *sb)
{
	int mstat;
	struct termios term;

	term = sb->saved_term;
	term.c_oflag = 0;
	term.c_lflag = 0;
	term.c_cc[VMIN] = 0;
	term.c_cc[VTIME] = 1;

	term.c_cflag = CLOCAL | CREAD | CS8 | HUPCL;
	term.c_iflag = IGNBRK | IGNPAR | IXON | IXOFF;

	cfsetispeed(&term, B9600);
	cfsetospeed(&term, B9600);

	if(tcsetattr(sb->fd, TCSAFLUSH, &term) == -1) {
		perror("sball_open: tcsetattr");
		return -1;
	}
	tcflush(sb->fd, TCIOFLUSH);

	mstat = sb->saved_mstat | TIOCM_DTR | TIOCM_RTS;
	ioctl(sb->fd, TIOCMGET, &mstat);
	return 0;
}

/* Logicad magellan spacemouse: 9600 8n2 CTS/RTS
 * Since the magellan devices don't seem to send any newlines, we can rely on
 * canonical mode to feed us nice whole lines at a time.
 */
static int stty_mag(struct sball *sb)
{
	int mstat;
	struct termios term;

	term = sb->saved_term;
	term.c_oflag = 0;
	term.c_lflag = ICANON;
	term.c_cc[VMIN] = 0;
	term.c_cc[VTIME] = 0;
	term.c_cc[VEOF] = 0;
	term.c_cc[VEOL] = '\r';
	term.c_cc[VEOL2] = 0;
	term.c_cc[VERASE] = 0;
	term.c_cc[VKILL] = 0;

	term.c_cflag = CLOCAL | CREAD | CS8 | CSTOPB | HUPCL;
#ifdef CCTS_OFLOW
	term.c_cflag |= CCTS_OFLOW;
#elif defined(CRTSCTS)
	term.c_cflag |= CRTSCTS;
#endif
	term.c_iflag = IGNBRK | IGNPAR;

	cfsetispeed(&term, B9600);
	cfsetospeed(&term, B9600);

	if(tcsetattr(sb->fd, TCSAFLUSH, &term) == -1) {
		perror("sball_open: tcsetattr");
		return -1;
	}
	tcflush(sb->fd, TCIOFLUSH);

	mstat = sb->saved_mstat | TIOCM_DTR | TIOCM_RTS;
	ioctl(sb->fd, TIOCMGET, &mstat);
	return 0;
}

static void stty_save(struct sball *sb)
{
	tcgetattr(sb->fd, &sb->saved_term);
	ioctl(sb->fd, TIOCMGET, &sb->saved_mstat);
}

static void stty_restore(struct sball *sb)
{
	tcsetattr(sb->fd, TCSAFLUSH, &sb->saved_term);
	tcflush(sb->fd, TCIOFLUSH);
	ioctl(sb->fd, TIOCMSET, &sb->saved_mstat);
}


static int proc_input(struct sball *sb)
{
	int sz;
	char *bptr = sb->buf;
	char *start = sb->buf;
	char *end = sb->buf + sb->len;

	/* see if we have a CR in the buffer */
	while(bptr < end) {
		if(*bptr == '\r') {
			*bptr = 0;
			sb->parse(sb, *start, start + 1, bptr - start - 1);
			start = ++bptr;
		} else {
			bptr++;
		}
	}

	sz = start - sb->buf;
	if(sz > 0) {
		memmove(sb->buf, start, sz);
		sb->len -= sz;
	}
	return 0;
}

static int mag_parsepkt(struct sball *sb, int id, char *data, int len)
{
	int i, prev, motion_pending = 0;
	unsigned int prev_key;

	/*printf("magellan packet: %c - %s (%d bytes)\n", (char)id, data, len);*/

	switch(id) {
	case 'd':
		if(len != 24) {
			fprintf(stderr, "magellan: invalid data packet, expected 24 bytes, got: %d\n", len);
			return -1;
		}
		for(i=0; i<6; i++) {
			prev = sb->mot[i];
			sb->mot[i] = ((((int)data[0] & 0xf) << 12) | (((int)data[1] & 0xf) << 8) |
					(((int)data[2] & 0xf) << 4) | (data[3] & 0xf)) - 0x8000;
			data += 4;

			if(sb->mot[i] != prev) {
				enqueue_motion(sb, i, sb->mot[i]);
				motion_pending++;
			}
		}
		if(motion_pending) {
			enqueue_motion(sb, -1, 0);
		}
		break;

	case 'k':
		if(len < 3) {
			fprintf(stderr, "magellan: invalid keyboard pakcet, expected 3 bytes, got: %d\n", len);
			return -1;
		}
		prev_key = sb->keystate;
		sb->keystate = (data[0] & 0xf) | ((data[1] & 0xf) << 4) | (((unsigned int)data[2] & 0xf) << 8);
		if(len > 3) {
			sb->keystate |= ((unsigned int)data[3] & 0xf) << 12;
		}

		if(sb->keystate != prev_key) {
			gen_button_events(sb, prev_key);
		}
		break;

	case 'e':
		if(data[0] == 1) {
			fprintf(stderr, "magellan error: illegal command: %c%c\n", data[1], data[2]);
		} else if(data[0] == 2) {
			fprintf(stderr, "magellan error: framing error\n");
		} else {
			fprintf(stderr, "magellan error: unknown device error\n");
		}
		return -1;

	default:
		break;
	}
	return 0;
}

static int sball_parsepkt(struct sball *sb, int id, char *data, int len)
{
	int i, prev, motion_pending = 0;
	char c, *rd, *wr;
	unsigned int prev_key;

	/* decode data packet, replacing escaped values with the correct ones */
	rd = wr = data;
	while(rd < data + len) {
		if((c = *rd++) == '^') {
			switch(*rd++) {
			case 'Q':
				*wr++ = 0x11;	/* XON */
				break;
			case 'S':
				*wr++ = 0x13;	/* XOFF */
				break;
			case 'M':
				*wr++ = 13;		/* CR */
				break;
			case '^':
				*wr++ = '^';
				break;
			default:
				fprintf(stderr, "sball decode: ignoring invalid escape code: %xh\n", (unsigned int)c);
			}
		} else {
			*wr++ = c;
		}
	}
	len = wr - data;	/* update the decoded length */

	switch(id) {
	case 'D':
		if(len != 14) {
			fprintf(stderr, "sball: invalid data packet, expected 14 bytes, got: %d\n", len);
			return -1;
		}

#ifndef SBALL_BIG_ENDIAN
		rd = data;
		for(i=0; i<6; i++) {
			rd += 2;
			c = rd[0];
			rd[0] = rd[1];
			rd[1] = c;
		}
#endif

		for(i=0; i<6; i++) {
			char *dest = (char*)(sb->mot + i);
			data += 2;
			prev = sb->mot[i];
			*dest++ = data[0];
			*dest++ = data[1];

			if(sb->mot[i] != prev) {
				enqueue_motion(sb, i, sb->mot[i]);
				motion_pending++;
			}
		}
		if(motion_pending) {
			enqueue_motion(sb, -1, 0);
		}
		break;

	case 'K':
		if(len != 2) {
			fprintf(stderr, "sball: invalid key packet, expected 2 bytes, got: %d\n", len);
			return -1;
		}
		if(sb->flags & SB4000) break;	/* ignore K packets from spaceball 4000 devices */

		prev_key = sb->keystate;
		/* data[1] bits 0-3 -> buttons 0,1,2,3
		 * data[1] bits 4,5 (3003 L/R) -> buttons 0, 1
		 * data[0] bits 0-2 -> buttons 4,5,6
		 * data[0] bit 4 is (2003 pick) -> button 7
		 */
		sb->keystate = ((data[1] & 0xf) | ((data[1] >> 4) & 3) | ((data[0] & 7) << 4) |
			((data[0] & 0x10) << 3)) & sb->keymask;

		if(sb->keystate != prev_key) {
			gen_button_events(sb, prev_key);
		}
		break;

	case '.':
		if(len != 2) {
			fprintf(stderr, "sball: invalid sb4k key packet, expected 2 bytes, got: %d\n", len);
			return -1;
		}
		/* spaceball 4000 key packet */
		if(!(sb->flags & SB4000)) {
			printf("Switching to spaceball 4000flx/5000flx-a mode (12 buttons)            \n");
			sb->flags |= SB4000;
			sb->nbuttons = 12;	/* might have guessed 8 before */
			sb->keymask = 0xfff;
		}
		/* update orientation flag (actually don't bother) */
		/*
		if(data[0] & 0x20) {
			sb->flags |= FLIPXY;
		} else {
			sb->flags &= ~FLIPXY;
		}
		*/

		prev_key = sb->keystate;
		/* data[1] bits 0-5 -> buttons 0,1,2,3,4,5
		 * data[1] bit 7 -> button 6
		 * data[0] bits 0-4 -> buttons 7,8,9,10,11
		 */
		sb->keystate = (data[1] & 0x3f) | ((data[1] & 0x80) >> 1) | ((data[0] & 0x1f) << 7);
		if(sb->keystate != prev_key) {
			gen_button_events(sb, prev_key);
		}
		break;

	case 'E':
		fprintf(stderr, "sball: error:");
		for(i=0; i<len; i++) {
			if(isprint((int)data[i])) {
				fprintf(stderr, " %c", data[i]);
			} else {
				fprintf(stderr, " %02xh", (unsigned int)data[i]);
			}
		}
		break;

	case 'M':	/* ignore MSS responses */
	case '?':	/* ignore unrecognized command errors */
		break;

	default:
		/* DEBUG */
		fprintf(stderr, "sball: got '%c' packet:", (char)id);
		for(i=0; i<len; i++) {
			fprintf(stderr, " %02x", (unsigned int)data[i]);
		}
		fputc('\n', stderr);
	}
	return 0;
}

static int guess_num_buttons(const char *verstr)
{
	int major, minor;
	const char *s;

	if((s = strstr(verstr, "Firmware version"))) {	/* spaceball */
		/* try to guess based on firmware number */
		if(sscanf(s + 17, "%d.%d", &major, &minor) == 2 && major == 2) {
			if(minor == 35 || minor == 62 || minor == 63) {
				return 2;	/* spaceball 3003/3003C */
			}
			if(minor == 43 || minor == 45) {
				return 12;	/* spaceball 4000flx/5000flx-a */
			}
			if(minor == 2 || minor == 13 || minor == 15 || minor == 42) {
				/* 2.42 is also used by spaceball 4000flx. we'll guess 2003c for
				 * now, and change the buttons to 12 first time we get a '.'
				 * packet. I'll also request a key report during init to make
				 * sure this happens as soon as possible, before clients have a
				 * chance to connect.
				 */
				return 8;	/* spaceball 1003/2003/2003c */
			}
		}
	}

	if(strstr(verstr, "MAGELLAN")) {
		return 9; /* magellan spacemouse */
	}

	if(strstr(verstr, "SPACEBALL")) {
		return 12; /* spaceball 5000 */
	}

	if(strstr(verstr, "CadMan")) {
		return 4;
	}

	fprintf(stderr, "Can't guess number of buttons, default to 8, report this as a bug!\n");
	return 8;
}

static void make_printable(char *buf, int len)
{
	int i, c;
	char *wr = buf;

	for(i=0; i<len; i++) {
		c = *buf++;
		if(c == '\r') {
			*wr++ = '\n';
			while(*buf == '\n' || *buf == '\r') buf++;
		} else {
			*wr++ = c;
		}
	}
	*wr = 0;
}

static int read_timeout(int fd, char *buf, int bufsz, long tm_usec)
{
	int res;
	long usec, sz = 0;
	struct timeval tv0, tv;
	fd_set rdset;

	if(!buf || bufsz <= 0) return -1;

	usec = tm_usec;
	gettimeofday(&tv0, 0);

	while(sz < bufsz && usec > 0) {
		tv.tv_sec = usec / 1000000;
		tv.tv_usec = usec % 1000000;

		FD_ZERO(&rdset);
		FD_SET(fd, &rdset);
		if((res = select(fd + 1, &rdset, 0, 0, &tv)) > 0 && FD_ISSET(fd, &rdset)) {
			sz += read(fd, buf + sz, bufsz - sz);
			buf[sz] = 0;
			tm_usec = usec = 128000;	/* wait 128ms for the rest of the message to appear */
			gettimeofday(&tv0, 0);
			continue;
		}
		if(res == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
			break;
		}
		gettimeofday(&tv, 0);
		usec = tm_usec - ((tv.tv_sec - tv0.tv_sec) * 1000000 + (tv.tv_usec - tv0.tv_usec));
	}

	return sz > 0 ? sz : -1;
}

static void enqueue_motion(struct sball *sb, int axis, int val)
{
	struct dev_input *inp = sb->evqueue + sb->evq_wr;

	sb->evq_wr = (sb->evq_wr + 1) & (EVQUEUE_SZ - 1);

	if(axis >= 0) {
		inp->type = INP_MOTION;
		inp->idx = axis;
		inp->val = val;
	} else {
		inp->type = INP_FLUSH;
	}
}

static void gen_button_events(struct sball *sb, unsigned int prev)
{
	int i;
	unsigned int bit = 1;
	unsigned int diff = sb->keystate ^ prev;
	struct dev_input *inp;

	for(i=0; i<16; i++) {
		if(diff & bit) {
			inp = sb->evqueue + sb->evq_wr;
			sb->evq_wr = (sb->evq_wr + 1) & (EVQUEUE_SZ - 1);

			inp->type = INP_BUTTON;
			inp->idx = i;
			inp->val = sb->keystate & bit ? 1 : 0;
		}
		bit <<= 1;
	}
}
