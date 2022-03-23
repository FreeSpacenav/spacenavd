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
#define DEF_PROTO_REQ_NAMES
#include "proto.h"
#undef DEF_PROTO_REQ_NAMES
#include "proto_unix.h"
#include "spnavd.h"
#ifdef USE_X11
#include "kbemu.h"
#endif


static int lsock = -1;


static int handle_request(struct client *c, struct reqresp *req);
static const char *reqstr(int req);

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
		if(!(c->evmask & EVMASK_MOTION)) break;

		data[0] = UEV_MOTION;

		motion_mul = get_client_sensitivity(c);
		for(i=0; i<6; i++) {
			float val = (float)ev->motion.data[i] * motion_mul;
			data[i + 1] = (int)val;
		}
		data[7] = ev->motion.period;
		break;

	case EVENT_BUTTON:
		if(!(c->evmask & EVMASK_BUTTON)) break;

		data[0] = ev->button.press ? UEV_PRESS : UEV_RELEASE;
		data[1] = ev->button.bnum;
		break;

	case EVENT_DEV:
		if(!(c->evmask & EVMASK_DEV)) break;

		data[0] = UEV_DEV;
		data[1] = ev->dev.op;
		data[2] = ev->dev.id;
		data[3] = ev->dev.devtype;
		data[4] = ev->dev.usbid[0];
		data[5] = ev->dev.usbid[1];
		break;

	case EVENT_CFG:
		if(!(c->evmask & EVMASK_CFG)) break;

		data[0] = UEV_CFG;
		data[1] = ev->cfg.cfg;
		memcpy(data + 2, ev->cfg.data, sizeof ev->cfg.data);
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
					while((rdbytes = read(s, &msg, sizeof msg)) < 0 && errno == EINTR);
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

						if(c->proto > 0) {
							/* set default event mask for proto-v1 clients */
							c->evmask = EVMASK_MOTION | EVMASK_BUTTON | EVMASK_DEV;
						}
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
					while((rdbytes = read(s, c->reqbuf + c->reqbytes, sizeof *req - c->reqbytes)) < 0 && errno == EINTR);
					if(rdbytes <= 0) {
						close(s);
						remove_client(c);
						continue;
					}
					c->reqbytes += rdbytes;
					if(c->reqbytes >= sizeof *req) {
						req = (struct reqresp*)c->reqbuf;
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

#define AXIS_VALID(x)	((x) >= 0 && (x) < MAX_AXES)
#define BN_VALID(x)		((x) >= 0 && (x) < MAX_BUTTONS)
#define BNACT_VALID(x)	((x) >= 0 && (x) < MAX_BNACT)

static int handle_request(struct client *c, struct reqresp *req)
{
	static char *serdev_end;
	static int serdev_total_len;

	int i, idx;
	float fval, fvec[6];
	struct device *dev;
	const char *str = 0;

	logmsg(LOG_DEBUG, "request %s - %x %x %x %x %x %x\n", reqstr(req->type), req->data[0],
			req->data[1], req->data[2], req->data[3], req->data[4], req->data[5], req->data[6]);

	switch(req->type & 0xffff) {
	case REQ_SET_NAME:
		memcpy(c->name, req->data, sizeof req->data);
		c->name[sizeof req->data] = 0;
		logmsg(LOG_INFO, "client name: %s\n", c->name);
		break;

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

	case REQ_SET_EVMASK:
		c->evmask = req->data[0];
		sendresp(c, req, 0);
		break;

	case REQ_GET_EVMASK:
		req->data[0] = c->evmask;
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

	case REQ_DEV_USBID:
		if((dev = get_client_device(c)) && dev->usbid[0] && dev->usbid[1]) {
			req->data[0] = dev->usbid[0];
			req->data[1] = dev->usbid[1];
			sendresp(c, req, 0);
		} else {
			sendresp(c, req, -1);
		}
		break;

	case REQ_DEV_TYPE:
		if((dev = get_client_device(c))) {
			req->data[0] = dev->type;
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

	case REQ_SCFG_DEADZONE:
		if(!AXIS_VALID(req->data[0])) {
			logmsg(LOG_WARNING, "client attempted to set invalid axis deadzone: %d\n", req->data[0]);
			sendresp(c, req, -1);
			return 0;
		}
		cfg.dead_threshold[req->data[0]] = req->data[1];
		sendresp(c, req, 0);
		break;

	case REQ_GCFG_DEADZONE:
		if(!AXIS_VALID(req->data[0])) {
			logmsg(LOG_WARNING, "client requested invalid axis deadzone: %d\n", req->data[0]);
			sendresp(c, req, -1);
			return 0;
		}
		req->data[1] = cfg.dead_threshold[req->data[0]];
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_INVERT:
		for(i=0; i<6; i++) {
			cfg.invert[i] = req->data[i] ? 1 : 0;
		}
		sendresp(c, req, 0);
		break;

	case REQ_GCFG_INVERT:
		memcpy(req->data, cfg.invert, 6 * sizeof(int));
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_AXISMAP:
		if(!AXIS_VALID(req->data[0]) || req->data[1] < -1 || req->data[1] >= 6) {
			logmsg(LOG_WARNING, "client attempted to set invalid axis mapping: %d -> %d\n",
					req->data[0], req->data[1]);
			sendresp(c, req, -1);
			return 0;
		}
		cfg.map_axis[req->data[0]] = req->data[1];
		sendresp(c, req, 0);
		break;

	case REQ_GCFG_AXISMAP:
		if(!AXIS_VALID(req->data[0])) {
			logmsg(LOG_WARNING, "client queried mapping of invalid axis: %d\n",
					req->data[0]);
			sendresp(c, req, -1);
			return 0;
		}
		req->data[1] = cfg.map_axis[req->data[0]];
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_BNMAP:
		if(!BN_VALID(req->data[0]) || !BN_VALID(req->data[1])) {
			logmsg(LOG_WARNING, "client attempted to set invalid button mapping: %d -> %d\n",
					req->data[0], req->data[1]);
			sendresp(c, req, -1);
			return 0;
		}
		cfg.map_button[req->data[0]] = req->data[1];
		sendresp(c, req, 0);
		break;

	case REQ_GCFG_BNMAP:
		if(!BN_VALID(req->data[0])) {
			logmsg(LOG_WARNING, "client queried mapping of invalid button: %d\n", req->data[0]);
			sendresp(c, req, -1);
			return 0;
		}
		req->data[1] = cfg.map_button[req->data[0]];
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_BNACTION:
		if(!BN_VALID(req->data[0]) || !BNACT_VALID(req->data[1])) {
			logmsg(LOG_WARNING, "client attempted to set invalid button action: %d -> %d\n",
					req->data[0], req->data[1]);
			sendresp(c, req, -1);
			return 0;
		}
		cfg.bnact[req->data[0]] = req->data[1];
		sendresp(c, req, 0);
		break;

	case REQ_GCFG_BNACTION:
		if(!BN_VALID(req->data[0])) {
			logmsg(LOG_WARNING, "client queried action bound to invalid button: %d\n", req->data[0]);
			sendresp(c, req, -1);
			return 0;
		}
		req->data[1] = cfg.bnact[req->data[0]];
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_KBMAP:
#ifdef USE_X11
		idx = req->data[0];
		if(!BN_VALID(idx) || (req->data[1] && !(str = kbemu_keyname(req->data[1])))) {
			logmsg(LOG_WARNING, "client attempted to set invalid key map: %d -> %x\n",
					idx, (unsigned int)req->data[1]);
			sendresp(c, req, -1);
			return 0;
		}
		cfg.kbmap[idx] = req->data[1];
		free(cfg.kbmap_str[idx]);
		cfg.kbmap_str[idx] = req->data[1] ? strdup(str) : 0;
		sendresp(c, req, 0);
#else
		logmsg(LOG_WARNING, "unable to set keyboard mappings, daemon compiled without X11 support\n");
		sendresp(c, req, -1);
#endif
		break;

	case REQ_GCFG_KBMAP:
#ifdef USE_X11
		idx = req->data[0];
		if(!BN_VALID(idx)) {
			logmsg(LOG_WARNING, "client queried keyboard mapping for invalid button: %d\n", idx);
			sendresp(c, req, -1);
			return 0;
		}
		if(cfg.kbmap_str[idx]) {
			if(!cfg.kbmap[idx]) {
				cfg.kbmap[idx] = kbemu_keysym(cfg.kbmap_str[idx]);
			}
			req->data[1] = cfg.kbmap[idx];
		} else {
			req->data[1] = 0;
		}
		sendresp(c, req, 0);
#else
		logmsg(LOG_WARNING, "unable to query keyboard mappings, daemon compiled without X11 support\n");
		sendresp(c, req, -1);
#endif
		break;

	case REQ_SCFG_SWAPYZ:
		cfg.swapyz = req->data[0] ? 1 : 0;
		sendresp(c, req, 0);

	case REQ_GCFG_SWAPYZ:
		req->data[0] = cfg.swapyz;
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_LED:
		if(req->data[0] < 0 || req->data[0] >= 3) {
			sendresp(c, req, -1);
			break;
		}
		cfg.led = req->data[0];
		cfg_changed();
		sendresp(c, req, 0);
		break;

	case REQ_GCFG_LED:
		req->data[0] = cfg.led;
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_GRAB:
		cfg.grab_device = req->data[0] ? 1 : 0;
		sendresp(c, req, 0);
		break;

	case REQ_GCFG_GRAB:
		req->data[0] = cfg.grab_device;
		sendresp(c, req, 0);
		break;

	case REQ_SCFG_SERDEV:
		if(!serdev_end) {
			/* first part */
			serdev_end = cfg.serial_dev;
			serdev_total_len = req->data[0];
		}
		for(i=0; i<6; i++) {
			if(serdev_end < serdev_end + PATH_MAX - 1) {
				*serdev_end++ = req->data[i + 1];
			}
			req->data[0]--;
		}
		if(req->data[0] <= 0) {
			*serdev_end = 0;
			if(strlen(cfg.serial_dev) != serdev_total_len) {
				logmsg(LOG_WARNING, "config SCFG_SERDEV, expected %d bytes, got %d\n", serdev_total_len, strlen(cfg.serial_dev));
			}
			serdev_end = 0;
			serdev_total_len = 0;
			cfg_changed();
		}
		break;

	case REQ_GCFG_SERDEV:
		req->data[0] = strlen(cfg.serial_dev);
		sendresp(c, req, 0);
		write(get_client_socket(c), cfg.serial_dev, req->data[0]);
		break;

	case REQ_CFG_SAVE:
		sendresp(c, req, write_cfg(cfgfile, &cfg));
		break;

	case REQ_CFG_RESTORE:
		sendresp(c, req, 0);	/* always succeed immediately */
		if(read_cfg(cfgfile, &cfg) == -1) {
			logmsg(LOG_INFO, "config restore requested but failed to read %s, restoring defaults instead\n",
					cfgfile);
			default_cfg(&cfg);
		}
		cfg_changed();
		break;

	case REQ_CFG_RESET:
		sendresp(c, req, 0);	/* always succeed immediately */
		default_cfg(&cfg);
		cfg_changed();
		break;

	default:
		logmsg(LOG_WARNING, "invalid client request: %s\n", reqstr(req->type));
		sendresp(c, req, -1);
	}

	return 0;
}

static const char *reqstr(int req)
{
	static char buf[8];

	req &= 0xffff;

	if(req >= 0x1000 && req < 0x1000 + sizeof reqnames_1000 / sizeof *reqnames_1000) {
		return reqnames_1000[req - 0x1000];
	}
	if(req >= 0x2000 && req < 0x2000 + sizeof reqnames_2000 / sizeof *reqnames_2000) {
		return reqnames_2000[req - 0x2000];
	}
	if(req >= 0x3000 && req < 0x3000 + sizeof reqnames_3000 / sizeof *reqnames_3000) {
		return reqnames_3000[req - 0x3000];
	}
	switch(req) {
	case REQ_CFG_SAVE:
		return "CFG_SAVE";
	case REQ_CFG_RESTORE:
		return "CFG_RESTORE";
	case REQ_CFG_RESET:
		return "CFG_RESET";
	default:
		break;
	}

	sprintf(buf, "0x%04x", req);
	return buf;
}
