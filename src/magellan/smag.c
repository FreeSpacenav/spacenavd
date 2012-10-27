/*
serial magellan device support for spacenavd

Copyright (C) 2012 John Tsiombikas <nuclear@member.fsf.org>
Copyright (C) 2010 Thomas Anderson <ta@nextgenengineering.com>

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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "magellan/smag.h"
#include "magellan/smag_comm.h"
#include "magellan/smag_event.h"
#include "magellan/serialconstants.h"

static void gen_disp_events(int *newval);
static void proc_disp_packet(void);
static void gen_button_event(int button, int new_state);
static void read_copy(void);
static void proc_disp_packet(void);
static void proc_bn_k_packet(void);
static void proc_bn_c_packet(void);
static void proc_bn_n_packet(void);
static void proc_bn_q_packet(void);
static void clean_input();


static int dev_fd;

struct input_struct {
	char rbuf[MAXREADSIZE];
	int rbuf_sz;
	char packet_buf[MAXPACKETSIZE];
	int packet_buf_pos;
	struct smag_event *evhead;
	struct smag_event *evtail;
} input;

static int first_byte_parity[16] = {
	0xE0, 0xA0, 0xA0, 0x60, 0xA0, 0x60, 0x60, 0xA0,
	0x90, 0x50, 0x50, 0x90, 0xD0, 0x90, 0x90, 0x50
};

static int second_byte_parity[64] = {
	0x80, 0x40, 0x40, 0x80, 0x40, 0x80, 0x80, 0x40,
	0x40, 0x80, 0x80, 0x40, 0x80, 0x40, 0x40, 0x80,
	0x40, 0x80, 0x80, 0x40, 0x80, 0x40, 0x40, 0x80,
	0x80, 0x40, 0x40, 0x80, 0xC0, 0x80, 0x80, 0x40,
	0xC0, 0x80, 0x80, 0x40, 0x80, 0x40, 0x40, 0x80,
	0x80, 0x40, 0x40, 0x80, 0x40, 0x80, 0x80, 0x40,
	0x80, 0x40, 0x40, 0x80, 0x40, 0x80, 0x80, 0x40,
	0x40, 0x80, 0x80, 0x40, 0x80, 0x40, 0x00, 0x80
};

void smag_init_device(int fd)
{
	smag_write(fd, "", 0);
	smag_write(fd, "\r\rm0", 4);
	smag_write(fd, "pAA", 3);
	smag_write(fd, "q00", 3);	/*default translation and rotation */
	smag_write(fd, "nM", 2);	/*zero radius. 0-15 defaults to 13 */
	smag_write(fd, "z", 1);		/*zero device */
	smag_write(fd, "c33", 3);	/*set translation, rotation on and dominant axis off */
	smag_write(fd, "l2\r\0", 4);
	smag_write(fd, "\r\r", 2);
	smag_write(fd, "l300", 4);
	smag_write(fd, "b9", 2);	/*these are beeps */
	smag_write(fd, "b9", 2);

	usleep(SMAG_DELAY_USEC);
	tcflush(fd, TCIOFLUSH);
	clean_input();
}

static void read_copy(void)
{
	int i;

	for(i=0; i<input.rbuf_sz; i++) {
		if(input.rbuf[i] == '\n' || input.rbuf[i] == '\r') {
			input.packet_buf[input.packet_buf_pos] = 0;	/* terminate string */

			if(input.packet_buf[0] == 'd' && input.packet_buf_pos == 15) {
				proc_disp_packet();
			} else if(input.packet_buf[0] == 'k' && input.packet_buf_pos == 4) {
				proc_bn_k_packet();
			} else if(input.packet_buf[0] == 'c' && input.packet_buf_pos == 3) {
				proc_bn_c_packet();
			} else if(input.packet_buf[0] == 'n' && input.packet_buf_pos == 2) {
				proc_bn_n_packet();
			} else if(input.packet_buf[0] == 'q' && input.packet_buf_pos == 3) {
				proc_bn_q_packet();
			} else {
				fprintf(stderr, "unknown packet   %s\n", input.packet_buf);
			}
			input.packet_buf_pos = 0;
		} else {
			input.packet_buf[input.packet_buf_pos] = input.rbuf[i];
			input.packet_buf_pos++;
			if(input.packet_buf_pos == MAXPACKETSIZE) {
				input.packet_buf_pos = 0;
				fprintf(stderr, "packet buffer overrun\n");
			}
		}
	}
}

int open_smag(const char *devfile)
{
	if((dev_fd = smag_open_device(devfile)) == -1) {
		return -1;
	}
	smag_set_port_magellan(dev_fd);
	smag_init_device(dev_fd);
	clean_input();
	return 0;
}

int close_smag()
{
	smag_write(dev_fd, "l000", 4);
	close(dev_fd);
	return 0;
}

int read_smag(struct dev_input *inp)
{
	/*need to return 1 if we fill in inp or 0 if no events */
	struct smag_event *ev;

	input.rbuf_sz = smag_read(dev_fd, input.rbuf, MAXREADSIZE);
	if(input.rbuf_sz > 0) {
		read_copy();
	}
	ev = input.evhead;
	if(ev) {
		input.evhead = input.evhead->next;

		*inp = ev->data;
		free_event(ev);
		return 1;
	}
	return 0;
}

int get_fd_smag()
{
	return dev_fd;
}

void get_version_string(int fd, char *buf, int sz)
{
	int bytesrd;
	char tmpbuf[MAXREADSIZE];

	smag_write(fd, "\r\rm0", 4);
	smag_write(fd, "", 0);
	smag_write(fd, "\r\rm0", 4);
	smag_write(fd, "c03", 3);
	smag_write(fd, "z", 1);
	smag_write(fd, "Z", 1);
	smag_write(fd, "l000", 4);
	usleep(SMAG_DELAY_USEC);
	tcflush(fd, TCIOFLUSH);
	clean_input();
	smag_write(fd, "vQ", 2);

	bytesrd = smag_read(fd, tmpbuf, MAXREADSIZE);
	if(bytesrd > 0 && bytesrd < sz) {
		strcpy(buf, tmpbuf);
	}
	clean_input();
}


static void gen_disp_events(int *newval)
{
	int i, pending;
	static int oldval[6] = {0, 0, 0, 0, 0, 0};
	struct smag_event *newev;

	pending = 0;
	for(i=0; i<6; i++) {
		if(newval[i] == oldval[i]) {
			continue;
		}
		oldval[i] = newval[i];

		newev = alloc_event();
		if(newev) {
			newev->data.type = INP_MOTION;
			newev->data.idx = i;
			newev->data.val = newval[i];
			newev->next = 0;

			if(input.evhead) {
				input.evtail->next = newev;
				input.evtail = newev;
			} else
				input.evhead = input.evtail = newev;
			pending = 1;
		}
	}

	if(pending) {
		newev = alloc_event();
		if(newev) {
			newev->data.type = INP_FLUSH;
			newev->next = 0;
		}

		if(input.evhead) {
			input.evtail->next = newev;
			input.evtail = newev;
		} else {
			input.evhead = input.evtail = newev;
		}
	}
}

static void proc_disp_packet(void)
{
	int i, last_bytes, offset, values[6];
	short int accum_last, number, accum_last_adj;

	accum_last = offset = 0;

	for(i=1; i<13; i+=2) {
		/*first byte check */
		unsigned char low, up;

		low = input.packet_buf[i] & 0x0F;
		up = input.packet_buf[i] & 0xF0;
		if(up != first_byte_parity[low]) {
			fprintf(stderr, "bad first packet\n");
			return;
		}

		/*second byte check */
		low = input.packet_buf[i + 1] & 0x3F;
		up = input.packet_buf[i + 1] & 0xC0;
		if(up != second_byte_parity[low]) {
			fprintf(stderr, "bad second packet\n");
			return;
		}

		number = (short int)((input.packet_buf[i] << 6 & 0x03C0) | (input.packet_buf[i + 1] & 0x3F));
		if(number > 512) {
			number -= 1024;
		}
		accum_last += number;

		if(number < 0) {
			offset += ((int)(number + 1) / 64) - 1;
		} else {
			offset += (int)number / 64;
		}
		/*printf("%8i ", number); */
		values[(i + 1) / 2 - 1] = number;
	}

	/*last byte of packet is a sum of 6 numbers and a factor of 64. use as a packet check.
	   still not sure what the second to last byte is for. */
	accum_last_adj = accum_last & 0x003F;
	accum_last_adj += offset;
	if(accum_last_adj < 0) {
		accum_last_adj += 64;
	}
	if(accum_last_adj > 63) {
		accum_last_adj -= 64;
	}

	last_bytes = (short int)(input.packet_buf[14] & 0x3F);

	if(accum_last_adj != last_bytes) {
		printf("   bad packet\n");
		return;
	}
	gen_disp_events(values);
	return;
}

static void gen_button_event(int button, int new_state)
{
	struct smag_event *newev = alloc_event();

	if(!newev) {
		return;
	}

	newev->data.type = INP_BUTTON;
	newev->data.idx = button;
	newev->data.val = new_state;
	newev->next = 0;

	if(input.evhead) {
		input.evtail->next = newev;
		input.evtail = newev;
	} else {
		input.evhead = input.evtail = newev;
	}
}

static void proc_bn_k_packet(void)
{
	static char old_state[5] = { 0, 0, 0, 0, 0 };

	if(input.packet_buf[1] != old_state[1]) {
		if((input.packet_buf[1] & 0x01) != (old_state[1] & 0x01)) {
			gen_button_event(0, input.packet_buf[1] & 0x01);
		}
		if((input.packet_buf[1] & 0x02) != (old_state[1] & 0x02)) {
			gen_button_event(1, input.packet_buf[1] & 0x02);
		}
		if((input.packet_buf[1] & 0x04) != (old_state[1] & 0x04)) {
			gen_button_event(2, input.packet_buf[1] & 0x04);
		}
		if((input.packet_buf[1] & 0x08) != (old_state[1] & 0x08)) {
			gen_button_event(3, input.packet_buf[1] & 0x08);
		}
	}

	if(input.packet_buf[2] != old_state[2]) {
		if((input.packet_buf[2] & 0x01) != (old_state[2] & 0x01)) {
			gen_button_event(4, input.packet_buf[2] & 0x01);
		}
		if((input.packet_buf[2] & 0x02) != (old_state[2] & 0x02)) {
			gen_button_event(5, input.packet_buf[2] & 0x02);
		}
		if((input.packet_buf[2] & 0x04) != (old_state[2] & 0x04)) {
			gen_button_event(6, input.packet_buf[2] & 0x04);
		}
		if((input.packet_buf[2] & 0x08) != (old_state[2] & 0x08)) {
			gen_button_event(7, input.packet_buf[2] & 0x08);
		}
	}

	/*skipping asterisk button. asterisk function come in through other packets. */
	/*magellan plus has left and right (10, 11) buttons not magellan classic */
	/*not sure if we need to filter out lower button events for magellan classic */

	if(input.packet_buf[3] != old_state[3]) {
		/*
		   if (input.packet_buf[3] & 0x01)
		   printf("button asterisk   ");
		 */
		if((input.packet_buf[3] & 0x02) != (old_state[3] & 0x02)) {
			gen_button_event(8, input.packet_buf[3] & 0x02);	/*left button */
		}
		if((input.packet_buf[3] & 0x04) != (old_state[3] & 0x04)) {
			gen_button_event(9, input.packet_buf[3] & 0x04);	/*right button */
		}
	}

	strcpy(old_state, input.packet_buf);
}

static void proc_bn_c_packet(void)
{
	/*these are implemented at device and these signals are to keep the driver in sync */
	if(input.packet_buf[1] & 0x02) {
		printf("translation is on   ");
	} else {
		printf("translation is off   ");
	}

	if(input.packet_buf[1] & 0x01) {
		printf("rotation is on   ");
	} else {
		printf("rotation is off   ");
	}

	if(input.packet_buf[1] & 0x04) {
		printf("dominant axis is on   ");
	} else {
		printf("dominant axis is off   ");
	}

	printf("\n");
	/*printf("%s\n", input.packet_buf); */
}

static void proc_bn_n_packet(void)
{
	int radius;

	radius = (int)input.packet_buf[1] & 0x0F;
	printf("zero radius set to %i\n", radius);
}

static void proc_bn_q_packet(void)
{
	/* this has no effect on the device numbers. Driver is to implement any scale of numbers */
	int rotation, translation;

	rotation = (int)input.packet_buf[1] & 0x07;
	translation = (int)input.packet_buf[2] & 0x07;
	printf("rotation = %i   translation = %i\n", rotation, translation);
}


static void clean_input(void)
{
	memset(input.rbuf, 0x00, MAXREADSIZE);
	input.rbuf_sz = 0;
	memset(input.packet_buf, 0x00, MAXPACKETSIZE);
	input.packet_buf_pos = 0;
}
