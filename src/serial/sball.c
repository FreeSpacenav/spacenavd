/*
spacenavd - a free software replacement driver for 6dof space-mice.
Copyright (C) 2007-2010 John Tsiombikas <nuclear@member.fsf.org>

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

This file incorporates work covered by the following copyright and
permission notice:

   Copyright 1997-2001 John E. Stone (j.stone@acm.org)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
   3. The name of the author may not be used to endorse or promote products
      derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
   EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
   OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
   IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
   OF SUCH DAMAGE.
*/

#define _POSIX_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "sball.h"
#include "sballserial.h"

struct event {
	struct dev_input data;
	struct event *next;
};

static struct event *ev_free_list;
int evpool_size;

static struct event *alloc_event(void);
static void free_event(struct event *ev);


typedef struct {
	SBallCommHandle commhandle;
	unsigned char buf[256];
	char resetstring[256];
	int bufpos;		/* current char position in packet buffer */
	int packtype;		/* what kind of packet is it */
	int packlen;		/* how many bytes do we ultimately expect? */
	int escapedchar;	/* if set, we're processing an escape sequence */
	int erroroccured;	/* if set, we've received an error packet or packets */
	int resetoccured;	/* if set, ball was reset, so have to reinitialize it */
	int spaceball4000;	/* if set, its a Spaceball 4000 */
	int leftymode4000;	/* if set, Spaceball 4000 in "lefty" orientation */
	int trans[3];		/* last translational data received */
	int rot[3];		/* last rotational data received */
	int buttons;		/* current button status */
	int timer;		/* time since last packet was received */
	int usenullregion;	/* software-implemented null region flag */
	int nulltrans[3];	/* translational null region values */
	int nullrot[3];		/* rotational null region values */

	/* event list added for spacenavd integration */
	struct event *evhead, *evtail;
} sballhandle;


static void generate_motion_events(sballhandle *handle, int *prev_val, int *new_val, int timer);
static void generate_button_events(sballhandle *handle, int prevstate, int newstate);


/* Spaceball 1003/2003 recommended initialization string.          */

/* Newer documentation suggests eliminating several of these       */

/* settings during initialization, leaving them at factory values. */
static char *initstring = "CB\rNT\rFTp\rFRp\rP@r@r\rMSSV\rZ\rBcCcC\r";

/* Reset spaceball and ideally determine model */
static void sball_hwreset(sballhandle * handle)
{
	/* Reset some state variables back to zero */
	handle->spaceball4000 = 0;	/* re-determine which type it is     */
	handle->leftymode4000 = 0;	/* re-determine if its in lefty mode */

	if(!handle->resetoccured) {
#if defined(DEBUG)
		printf("Sending reset command to spaceball...\n");
#endif
		handle->resetoccured = 1;
		sball_comm_write(handle->commhandle, "@\r");	/* force reset */
	}
#if 0
	/* give the spaceball time to reset itself */
	sleep(2);
#endif

#if defined(DEBUG)
	printf("Sending initialization sequence to spaceball...\n");
#endif

	sball_comm_write(handle->commhandle, initstring);	/* do remaining init */
}


SBallHandle sball_open(const char *sballname)
{
	sballhandle *handle;

	if(sballname == NULL)
		return NULL;

	handle = (sballhandle *) malloc(sizeof(sballhandle));
	if(handle == NULL)
		return NULL;

	/* clear all values in sballhandle to 0 */
	memset(handle, 0, sizeof(sballhandle));
	handle->packlen = 1;
	handle->resetoccured = 0;

	if(sball_comm_open(sballname, &handle->commhandle) == -1) {
		free(handle);
		return NULL;
	}

	sball_hwreset(handle);

	return handle;		/* successfull open */
}


int sball_close(SBallHandle voidhandle)
{
	sballhandle *handle = voidhandle;

	if(handle == NULL)
		return -1;

	sball_comm_close(&handle->commhandle);
	free(handle);
	return 0;		/* successfull close */
}


static int sball_update(SBallHandle voidhandle)
{
	int i, num, packs;

	unsigned char rawbuf[1024];

	sballhandle *handle = voidhandle;

	if(handle == NULL)
		return -1;

	packs = 0;		/* no packs received yet */

	num = sball_comm_read(handle->commhandle, (char *)rawbuf, 1023);

	if(num > 0) {
		for(i = 0; i < num; i++) {


			/* process potentially occuring escaped character sequences */
			if(rawbuf[i] == '^') {
				if(!handle->escapedchar) {
					handle->escapedchar = 1;
					continue;	/* eat the escape character from buffer */
				}
			}

			if(handle->escapedchar) {
				handle->escapedchar = 0;

				switch(rawbuf[i]) {
				case '^':	/* leave char in buffer unchanged */
					break;

				case 'Q':
				case 'S':
				case 'M':
					rawbuf[i] &= 0x1F;	/* convert character to unescaped form */
					break;

				default:
#if defined(DEBUG)
					printf("\nGot a bad escape sequence! 0x%02x", rawbuf[i]);
					if(isprint(rawbuf[i]))
						printf(" (%c)", rawbuf[i]);
					else
						printf(" (unprintable)");
					printf("\n");
#endif
					break;
				}
			}


			/* figure out what kind of packet we received */
			if(handle->bufpos == 0) {
				switch(rawbuf[i]) {
				case 'D':	/* Displacement packet */
					handle->packtype = 'D';
					handle->packlen = 16;	/* D packets are 15 bytes long */
					break;

				case 'K':	/* Button/Key packet */
					handle->packtype = 'K';
					handle->packlen = 4;	/* K packets are 3 bytes long */
					break;

				case '.':	/* Spaceball 4000 FLX "advanced" button press event */
					handle->packtype = '.';
					handle->packlen = 4;	/* . packets are 3 bytes long */
					break;

				case 'C':	/* Communications mode packet */
					handle->packtype = 'C';
					handle->packlen = 4;
					break;

				case 'F':	/* Spaceball sensitization mode packet */
					handle->packtype = 'F';
					handle->packlen = 4;
					break;

				case 'M':	/* Movement mode packet */
					handle->packtype = 'M';
					handle->packlen = 5;
					break;

				case 'N':	/* Null region packet */
					handle->packtype = 'N';
					handle->packlen = 3;
					break;

				case 'P':	/* Update rate packet */
					handle->packtype = 'P';
					handle->packlen = 6;
					break;

				case '\v':	/* XON at poweron */
					handle->packtype = '\v';
					handle->packlen = 1;
					break;

				case '\n':	/* carriage return at poweron */
				case '\r':	/* carriage return at poweron */
					handle->packtype = '\r';
					handle->packlen = 1;
					break;

				case '@':	/* Spaceball Hard/Soft Reset packet */
					handle->resetoccured = 1;
					handle->packtype = '@';
					handle->packlen = 62;	/* Resets aren't longer than 62 chars */
					break;

				case 'E':	/* Error packet */
					handle->packtype = 'E';
					handle->packlen = 8;	/* E packets are up to 7 bytes long */
					break;

				case 'Z':	/* Zero packet (Spaceball 2003/3003/4000 FLX) */
					handle->packtype = 'Z';
					handle->packlen = 14;	/* Z packets are hardware dependent */
					break;

				default:	/* Unknown packet! */
#if defined(DEBUG)
					printf("\nUnknown packet (1): 0x%02x \n ", rawbuf[i]);
					printf("                char:  ");
					if(isprint(rawbuf[i]))
						printf("%c", rawbuf[i]);
					else
						printf(" (unprintable)");
					printf("\n");
#endif
					continue;
				}
			}


			handle->buf[handle->bufpos] = rawbuf[i];
			handle->bufpos++;

			/* Reset packet processing */
			if(handle->packtype == '@') {
				if(rawbuf[i] != '\r')
					continue;
				else
					handle->packlen = handle->bufpos;
			}

			/* Error packet processing */
			if(handle->packtype == 'E') {
				if(rawbuf[i] != '\r')
					continue;
				else
					handle->packlen = handle->bufpos;
			} else if(handle->bufpos != handle->packlen)
				continue;

			switch(handle->packtype) {
			case 'D':	/* ball displacement event */
				/* modified by John Tsiombikas for spacenavd integration */
				{
					unsigned int tx, ty, tz, rx, ry, rz;
					int i, prev_val[6], new_val[6];

					/* number of 1/16ths of milliseconds since last */
					/* ball displacement packet */
					handle->timer = ((handle->buf[1]) << 8) | (handle->buf[2]);

					tx = ((handle->buf[3]) << 8) | ((handle->buf[4]));
					ty = ((handle->buf[5]) << 8) | ((handle->buf[6]));
					tz = ((handle->buf[7]) << 8) | ((handle->buf[8]));
					rx = ((handle->buf[9]) << 8) | ((handle->buf[10]));
					ry = ((handle->buf[11]) << 8) | ((handle->buf[12]));
					rz = ((handle->buf[13]) << 8) | ((handle->buf[14]));

					for(i=0; i<3; i++) {
						prev_val[i] = handle->trans[i];
						prev_val[i + 3] = handle->rot[i];
					}

					new_val[0] = (((int)tx) << 16) >> 16;
					new_val[1] = (((int)ty) << 16) >> 16;
					new_val[2] = (((int)tz) << 16) >> 16;
					new_val[3] = (((int)rx) << 16) >> 16;
					new_val[4] = (((int)ry) << 16) >> 16;
					new_val[5] = (((int)rz) << 16) >> 16;

					generate_motion_events(handle, prev_val, new_val, handle->timer);

					for(i=0; i<3; i++) {
						handle->trans[i] = new_val[i];
						handle->rot[i] = new_val[i + 3];
					}
				}
				break;

			case 'K':	/* button press event */
				/* modified by John Tsiombikas for spacenavd integration */
				{
					int newstate;

					/* Spaceball 2003A, 2003B, 2003 FLX, 3003 FLX, 4000 FLX       */
					/* button packet. (4000 only for backwards compatibility)     */
					/* The lowest 5 bits of the first byte are buttons 5-9        */
					/* Button '8' on a Spaceball 2003 is the rezero button        */
					/* The lowest 4 bits of the second byte are buttons 1-4       */
					/* For Spaceball 2003, we'll map the buttons 1-7 normally     */
					/* skip 8, as its a hardware "rezero button" on that device   */
					/* and call the "pick" button "8".                            */
					/* On the Spaceball 3003, the "right" button also triggers    */
					/* the "pick" bit.  We OR the 2003/3003 rezero bits together  */

					/* if we have found a Spaceball 4000, then we ignore the 'K'  */
					/* packets entirely, and only use the '.' packets.            */
					if(handle->spaceball4000)
						break;

					newstate = ((handle->buf[1] & 0x10) << 3) |	/* 2003 pick button is "8" */
						((handle->buf[1] & 0x20) << 9) |	/* 3003 rezero button      */
						((handle->buf[1] & 0x08) << 11) |	/* 2003 rezero button      */
						((handle->buf[1] & 0x07) << 4) |	/* 5,6,7    (2003/4000)    */
						((handle->buf[2] & 0x30) << 8) |	/* 3003 Left/Right buttons */
						((handle->buf[2] & 0x0F));	/* 1,2,3,4  (2003/4000)    */

					generate_button_events(handle, handle->buttons, newstate);
					handle->buttons = newstate;
				}
				break;

			case '.':	/* button press event (4000) */
				/* modified by John Tsiombikas for spacenavd integration */
				{
					int newstate;
					/* Spaceball 4000 FLX "expanded" button packet, with 12 buttons */

					/* extra packet validity check, since we use this packet type */
					/* to override the 'K' button packets, and determine if its a */
					/* Spaceball 4000 or not...                                   */
					if(handle->buf[3] != '\r') {
						break;	/* if not terminated with a '\r', probably garbage */
					}

					/* if we got a valid '.' packet, this must be a Spaceball 4000 */
#if defined(DEBUG)
					if(!handle->spaceball4000)
						printf("\nDetected a Spaceball 4000 FLX\n");
#endif
					handle->spaceball4000 = 1;	/* Must be talking to a Spaceball 4000 */

					/* Spaceball 4000 series "expanded" button press event      */
					/* includes data for 12 buttons, and left/right orientation */
					newstate = (((~handle->buf[1]) & 0x20) << 10) |	/* "left handed" mode  */
						((handle->buf[1] & 0x1F) << 7) |	/* 8,9,10,11,12        */
						((handle->buf[2] & 0x3F)) |	/* 1,2,3,4,5,6 (4000)  */
						((handle->buf[2] & 0x80) >> 1);	/* 7           (4000)  */

					generate_button_events(handle, handle->buttons, newstate);
					handle->buttons = newstate;

#if defined(DEBUG)
					if(handle->leftymode4000 != ((handle->buf[1] & 0x20) == 0))
						printf("\nSpaceball 4000 mode changed to: %s\n",
							   (((handle->buf[1] & 0x20) ==
							 0) ? "left handed" : "right handed"));
#endif
					/* set "lefty" orientation mode if "lefty bit" is _clear_ */
					if((handle->buf[1] & 0x20) == 0)
						handle->leftymode4000 = 1;	/* left handed mode */
					else
						handle->leftymode4000 = 0;	/* right handed mode */
				}
				break;

			case 'C':	/* Communications mode packet */
			case 'F':	/* Spaceball sensitization packet */
			case 'P':	/* Spaceball update rate packet */
			case 'M':	/* Spaceball movement mode packet */
			case 'N':	/* Null region packet */
			case '\r':	/* carriage return at poweron */
			case '\v':	/* XON at poweron */
				/* eat and ignore these packets */
				break;

			case '@':	/* Reset packet */
#ifdef DEBUG
				printf("Spaceball reset: ");
				for(j = 0; j < handle->packlen; j++) {
					if(isprint(handle->buf[j]))
						printf("%c", handle->buf[j]);
				}
				printf("\n");
#endif
				/* if we get a reset packet, we have to re-initialize       */
				/* the device, and assume that its completely schizophrenic */
				/* at this moment, we must reset it again at this point     */
				handle->resetoccured = 1;
				sball_hwreset(handle);
				break;


			case 'E':	/* Error packet, hardware/software problem */
				handle->erroroccured++;
#ifdef DEBUG
				printf("\nSpaceball Error!!    ");
				printf("Error code: ");
				for(j = 0; j < handle->packlen; j++) {
					printf(" 0x%02x ", handle->buf[j]);
				}
				printf("\n");
#endif
				break;

			case 'Z':	/* Zero packet (Spaceball 2003/3003/4000 FLX) */
				/* We just ignore these... */
				break;

			default:
#ifdef DEBUG
				printf("Unknown packet (2): 0x%02x\n", handle->packtype);
				printf("              char:  ");
				if(isprint(handle->packtype))
					printf("%c", handle->packtype);
				else
					printf(" (unprintable)");
				printf("\n");
#endif
				break;
			}

			/* reset */
			handle->bufpos = 0;
			handle->packtype = 0;
			handle->packlen = 1;
			packs++;
		}
	}

	return packs;
}


int sball_rezero(SBallHandle voidhandle)
{
	sballhandle *handle = voidhandle;

	char outbuf[200];

	if(handle == NULL)
		return -1;

	sprintf(outbuf, "\rZ\r");
	sball_comm_write(handle->commhandle, outbuf);

	return 0;
}

int sball_init(SBallHandle voidhandle)
{
	sballhandle *handle = voidhandle;

	/*char outbuf[200]; */

	if(handle == NULL)
		return -1;

	sball_update(handle);

#if 0
	sprintf(outbuf, "\r");
	sball_update(handle);
	sball_comm_write(handle->commhandle, outbuf);
	sball_rezero(handle);
#endif

	return 0;
}


void sball_set_nullregion(SBallHandle voidhandle,
			  int nulltx, int nullty, int nulltz, int nullrx, int nullry, int nullrz)
{
	sballhandle *handle = voidhandle;

	handle->nulltrans[0] = abs(nulltx);
	handle->nulltrans[1] = abs(nullty);
	handle->nulltrans[2] = abs(nulltz);

	handle->nullrot[0] = abs(nullrx);
	handle->nullrot[1] = abs(nullry);
	handle->nullrot[2] = abs(nullrz);

	handle->usenullregion = 1;
}


static int nullregion(int null, int val)
{
	if(abs(val) > null) {
		return ((val > 0) ? (val - null) : (val + null));
	}
	return 0;
}

static void sball_do_nullregion(SBallHandle voidhandle)
{
	sballhandle *handle = voidhandle;

	handle->trans[0] = nullregion(handle->nulltrans[0], handle->trans[0]);
	handle->trans[1] = nullregion(handle->nulltrans[1], handle->trans[1]);
	handle->trans[2] = nullregion(handle->nulltrans[2], handle->trans[2]);
	handle->rot[0] = nullregion(handle->nullrot[0], handle->rot[0]);
	handle->rot[1] = nullregion(handle->nullrot[1], handle->rot[1]);
	handle->rot[2] = nullregion(handle->nullrot[2], handle->rot[2]);
}

int sball_getstatus(SBallHandle voidhandle, int *tx, int *ty, int *tz,
		    int *rx, int *ry, int *rz, int *buttons)
{
	sballhandle *handle = voidhandle;

	int events;

	if(handle == NULL)
		return -1;

	events = sball_update(handle);	/* check for new data */

	/* perform null region processing */
	if(handle->usenullregion)
		sball_do_nullregion(voidhandle);

	if(tx != NULL)
		*tx = handle->trans[0];
	if(ty != NULL)
		*ty = handle->trans[1];
	if(tz != NULL)
		*tz = handle->trans[2];

	if(rx != NULL)
		*rx = handle->rot[0];
	if(ry != NULL)
		*ry = handle->rot[1];
	if(rz != NULL)
		*rz = handle->rot[2];

	if(buttons != NULL)
		*buttons = handle->buttons;

	/* no timer code yet */

	return events;
}

/* everything from this point to the end of file was added by
 * John Tsiombikas for spacenavd integration.
 */
int sball_get_input(SBallHandle voidhandle, struct dev_input *inp)
{
	struct event *ev;
	sballhandle *handle = voidhandle;

	/* read pending packets from the device and append them in the event list */
	sball_update(handle);

	/* if there are any events in the list, grab the first and return it */
	if((ev = handle->evhead)) {
		handle->evhead = handle->evhead->next;

		*inp = ev->data;
		free_event(ev);
		return 1;
	}
	return 0;
}

int sball_get_fd(SBallHandle voidhandle)
{
	sballhandle *sball = voidhandle;

	return sball_comm_fd(sball->commhandle);
}

static struct event *alloc_event(void)
{
	struct event *ev;

	if(ev_free_list) {
		ev = ev_free_list;
		ev_free_list = ev->next;
	} else {
		ev = malloc(sizeof *ev);
		evpool_size++;
	}
	return ev;
}

static void free_event(struct event *ev)
{
	if(evpool_size > 512) {
		free(ev);
		evpool_size--;
	} else {
		ev->next = ev_free_list;
		ev_free_list = ev;
	}
}

static void generate_motion_events(sballhandle *handle, int *prev_val, int *new_val, int timer)
{
	int i, pending = 0;
	struct event *ev;

	for(i=0; i<6; i++) {
		if(prev_val[i] == new_val[i]) {
			continue;
		}

		if((ev = alloc_event())) {
			ev->data.type = INP_MOTION;
			ev->data.idx = i;
			ev->data.val = new_val[i];
			ev->next = 0;

			if(handle->evhead) {
				handle->evtail->next = ev;
				handle->evtail = ev;
			} else {
				handle->evhead = handle->evtail = ev;
			}
			pending = 1;
		}
	}

	if(pending) {
		if((ev = alloc_event())) {
			ev->data.type = INP_FLUSH;
			ev->next = 0;
		}

		if(handle->evhead) {
			handle->evtail->next = ev;
			handle->evtail = ev;
		} else {
			handle->evhead = handle->evtail = ev;
		}
	}
}

static void generate_button_events(sballhandle *handle, int prevstate, int newstate)
{
	int i;

	for(i=0; i<16; i++) {
		int newbit = (newstate >> i) & 1;
		if(newbit != ((prevstate >> i) & 1)) {
			/* state changed, trigger event */
			struct event *ev = alloc_event();
			if(!ev) continue;

			ev->data.type = INP_BUTTON;
			ev->data.idx = i;
			ev->data.val = newbit;
			ev->next = 0;

			if(handle->evhead) {
				handle->evtail->next = ev;
				handle->evtail = ev;
			} else {
				handle->evhead = handle->evtail = ev;
			}
		}
	}
}
