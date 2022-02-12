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
#ifndef PROTO_H_
#define PROTO_H_

/* maximum supported protocol version */
#define MAX_PROTO_VER	1

struct reqresp {
	int type;
	int data[7];
};

#define REQ_TAG			0x7faa0000
#define REQ_BASE		0x1000

/* REQ_S* are set, REQ_G* are get requests.
 * Quick-reference for request-response data in the comments next to each
 * request: Q[n] defines request data item n, R[n] defines response data item n
 *
 * status responses are 0 for success, non-zero for failure
 */
enum {
	/* per-client settings */
	REQ_SET_SENS = REQ_BASE,/* set client sensitivity:	Q[0] float - R[6] status */
	REQ_GET_SENS,			/* get client sensitivity:	R[0] float R[6] status */

	/* device queries */
	REQ_DEV_NAME = 0x2000,	/* get device name:	R[0] length R[6] status followed
							   by <length> bytes */
	REQ_DEV_PATH,			/* get device path: same as above */
	REQ_DEV_NAXES,			/* get number of axes:		R[0] num axes R[6] status */
	REQ_DEV_NBUTTONS,		/* get number of buttons: same as above */
	/* TODO: features like LCD, LEDs ... */

	/* configuration settings */
	REQ_SCFG_SENS = 0x3000,	/* set global sensitivity:	Q[0] float - R[6] status */
	REQ_GCFG_SENS,			/* get global sens:			R[0] float R[6] status */
	REQ_SCFG_SENS_AXIS,		/* set per-axis sens/ty:	Q[0-5] values - R[6] status */
	REQ_GCFG_SENS_AXIS,		/* get per-axis sens/ty:	R[0-5] values R[6] status */
	REQ_SCFG_DEADZONE,		/* set deadzones:			Q[0-5] values - R[6] status */
	REQ_GCFG_DEADZONE,		/* get deadzones:			R[0-5] values R[6] status */
	REQ_SCFG_INVERT,		/* set invert axes:			Q[0-5] invert - R[6] status */
	REQ_GCFG_INVERT,		/* get invert axes:			R[0-5] invert R[6] status */
	/* TODO ... more */

	REQ_CHANGE_PROTO	= 0x5500
};

#endif	/* PROTO_H_ */
