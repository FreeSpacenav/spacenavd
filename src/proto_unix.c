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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include "proto.h"
#include "proto_unix.h"
#include "spnavd.h"


enum {
	UEV_TYPE_MOTION,
	UEV_TYPE_PRESS,
	UEV_TYPE_RELEASE
};

static int lsock = -1;


static int handle_request(struct client *c, struct reqresp *req);


int init_unix(void)
{
	int s;
	mode_t prev_umask;
	struct sockaddr_un addr;

	if(lsock >= 0) return 0;

	if((s = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		logmsg(LOG_ERR, "failed to create socket: %s\n", strerror(errno));
		return -1;
	}

	unlink(SOCK_NAME);	/* in case it already exists */

	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, SOCK_NAME);

	prev_umask = umask(0);

	if(bind(s, (struct sockaddr*)&addr, sizeof addr) == -1) {
		logmsg(LOG_ERR, "failed to bind unix socket: %s: %s\n", SOCK_NAME, strerror(errno));
		close(s);
		return -1;
	}

	umask(prev_umask);

	if(listen(s, 8) == -1) {
		logmsg(LOG_ERR, "listen failed: %s\n", strerror(errno));
		close(s);
		unlink(SOCK_NAME);
		return -1;
	}

	lsock = s;
	return 0;
}

void close_unix(void)
{
	if(lsock != -1) {
		close(lsock);
		lsock = -1;

		unlink(SOCK_NAME);
	}
}

int get_unix_socket(void)
{
	return lsock;
}

void send_uevent(spnav_event *ev, struct client *c)
{
	int i, data[8] = {0};
	float motion_mul;

	if(lsock == -1) return;

	switch(ev->type) {
	case EVENT_MOTION:
		data[0] = UEV_TYPE_MOTION;

		motion_mul = get_client_sensitivity(c);
		for(i=0; i<6; i++) {
			float val = (float)ev->motion.data[i] * motion_mul;
			data[i + 1] = (int)val;
		}
		data[7] = ev->motion.period;
		break;

	case EVENT_BUTTON:
		data[0] = ev->button.press ? UEV_TYPE_PRESS : UEV_TYPE_RELEASE;
		data[1] = ev->button.bnum;
		break;

	default:
		break;
	}

	while(write(get_client_socket(c), data, sizeof data) == -1 && errno == EINTR);
}

int handle_uevents(fd_set *rset)
{
	struct client *citer;
	struct reqresp *req;

	if(lsock == -1) {
		return -1;
	}

	if(FD_ISSET(lsock, rset)) {
		/* got an incoming connection */
		int s;

		if((s = accept(lsock, 0, 0)) == -1) {
			logmsg(LOG_ERR, "error while accepting connection on the UNIX socket: %s\n", strerror(errno));
		} else {
			/* set socket as non-blocking and add client to the list */
			fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);

			if(!add_client(CLIENT_UNIX, &s)) {
				logmsg(LOG_ERR, "failed to add client: %s\n", strerror(errno));
			}
		}
	}

	/* all the UNIX socket clients */
	citer = first_client();
	while(citer) {
		struct client *c = citer;
		citer = next_client();

		if(get_client_type(c) == CLIENT_UNIX) {
			int s = get_client_socket(c);

			if(FD_ISSET(s, rset)) {
				int rdbytes, msg;
				float sens;

				/* handle client requests */
				switch(c->proto) {
				case 0:
					while((rdbytes = read(s, &msg, sizeof msg)) <= 0 && errno == EINTR);
					if(rdbytes <= 0) {	/* something went wrong... disconnect client */
						close(get_client_socket(c));
						remove_client(c);
						continue;
					}

					/* handle magic NaN protocol change requests */
					if((msg & 0xffffff00) == (REQ_TAG | REQ_CHANGE_PROTO)) {
						c->proto = msg & 0xff;

						/* if the client requests a protocol version higher than the
						 * daemon supports, return the maximum supported version and
						 * switch to that.
						 */
						if(c->proto > MAX_PROTO_VER) {
							c->proto = MAX_PROTO_VER;
							msg = REQ_TAG | REQ_CHANGE_PROTO | MAX_PROTO_VER;
						}
						write(s, &msg, sizeof msg);
						continue;
					}

					/* protocol v0: only sensitivity comes from clients */
					sens = *(float*)&msg;
					if(isfinite(sens)) {
						set_client_sensitivity(c, sens);
					}
					break;

				case 1:
					/* protocol v1: accumulate request bytes, and process */
					c->reqbytes += read(s, c->reqbuf + c->reqbytes, sizeof *req - c->reqbytes);
					if(c->reqbytes >= sizeof *req) {
						req = (struct reqresp*)c->reqbuf;
						/*
						logmsg(LOG_INFO, "DBG REQ (%d): %x - %x %x %x %x %x %x %x\n", c->reqbytes,
								req->type, req->data[0], req->data[1], req->data[2],
								req->data[3], req->data[4], req->data[5], req->data[6]);
						*/
						c->reqbytes = 0;
						if(handle_request(c, req) == -1) {
							close(s);
							remove_client(c);
						}
					}
					break;
				}
			}
		}
	}

	return 0;
}

static int sendresp(struct client *c, struct reqresp *rr, int status)
{
	rr->data[6] = status;
	return write(get_client_socket(c), rr, sizeof *rr);
}

static int handle_request(struct client *c, struct reqresp *req)
{
	int i;
	float fval, fvec[6];
	struct device *dev;

	switch(req->type & 0xffff) {
	case REQ_SET_SENS:
		fval = *(float*)req->data;
		if(isfinite(fval)) {
			set_client_sensitivity(c, fval);
			sendresp(c, req, 0);
		} else {
			logmsg(LOG_WARNING, "client attempted to set invalid client sensitivity\n");
			sendresp(c, req, -1);
		}
		break;

	case REQ_GET_SENS:
		fval = get_client_sensitivity(c);
		req->data[0] = *(int*)&fval;
		sendresp(c, req, 0);
		break;

	case REQ_DEV_NAME:
		if((dev = get_client_device(c))) {
			req->data[0] = strlen(dev->name);
			sendresp(c, req, 0);
			write(get_client_socket(c), dev->name, req->data[0]);
		} else {
			sendresp(c, req, -1);
		}
		break;

	case REQ_DEV_PATH:
		if((dev = get_client_device(c))) {
			req->data[0] = strlen(dev->name);
			sendresp(c, req, 0);
			write(get_client_socket(c), dev->path, req->data[0]);
		} else {
			sendresp(c, req, -1);
		}
		break;

	case REQ_DEV_NAXES:
		if((dev = get_client_device(c))) {
			req->data[0] = dev->num_axes;
			sendresp(c, req, 0);
		} else {
			sendresp(c, req, -1);
		}
		break;

	case REQ_DEV_NBUTTONS:
		if((dev = get_client_device(c))) {
			req->data[0] = dev->num_buttons;
			sendresp(c, req, 0);
		} else {
			sendresp(c, req, -1);
		}
		break;

	case REQ_SCFG_SENS:
		fval = *(float*)req->data;
		if(isfinite(fval)) {
			cfg.sensitivity = fval;
			sendresp(c, req, 0);
		} else {
			logmsg(LOG_WARNING, "client attempted to set invalid global sensitivity\n");
			sendresp(c, req, -1);
		}
		break;

	case REQ_GCFG_SENS:
		req->data[0] = *(int*)&cfg.sensitivity;
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_SENS_AXIS:
		for(i=0; i<6; i++) {
			fvec[i] = ((float*)req->data)[i];
			if(!isfinite(fvec[i])) {
				logmsg(LOG_WARNING, "client attempted to set invalid axis %d sensitivity\n", i);
				sendresp(c, req, -1);
				return 0;
			}
		}
		for(i=0; i<3; i++) {
			cfg.sens_trans[i] = fvec[i];
			cfg.sens_rot[i] = fvec[i + 3];
		}
		sendresp(c, req, 0);
		break;

	case REQ_GCFG_SENS_AXIS:
		for(i=0; i<3; i++) {
			req->data[i] = *(int*)(cfg.sens_trans + i);
			req->data[i + 3] = *(int*)(cfg.sens_rot + i);
		}
		sendresp(c, req, 0);
		break;

		/* TODO ... more */

	default:
		logmsg(LOG_WARNING, "invalid client request: %04xh\n", (unsigned int)req->type);
		sendresp(c, req, -1);
	}

	return 0;
}
