#ifndef PROTO_H_
#define PROTO_H_

#include "config.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif

/* maximum supported protocol version */
#define MAX_PROTO_VER	1

enum {
	UEV_MOTION,
	UEV_PRESS,
	UEV_RELEASE,
	UEV_DEV,
	UEV_CFG,
	UEV_RAWAXIS,
	UEV_RAWBUTTON,

	UEV_MOTION_MULTIDEV,
	UEV_PRESS_MULTIDEV,
	UEV_RELEASE_MULTIDEV,
	UEV_RAWAXIS_MULTIDEV,
	UEV_RAWBUTTON_MULTIDEV,
	MAX_UEV
};



struct reqresp {
	int32_t type;
	int32_t data[7];
};

struct reqresp_strbuf {
	char *buf, *endp;
	int size;
	int expect;
};

#define REQ_TAG			0x7faa0000
#define REQ_BASE		0x1000

/* REQ_S* are set requests, REQ_G* are get requests.
 * Quick-reference for request-response data in the comments next to each
 * request: Q[n] defines request data item n, R[n] defines response data item n
 *
 * status responses are 0 for success, non-zero for failure
 *
 * "remaining length" fields in string transfers have 16 valid bits. Bit 16 is
 * used as a flag: 0 for the first packet, 1 for continuation packets.
 */
enum {
	/* per-client settings */
	REQ_SET_NAME = REQ_BASE,/* set client name: Q[0-5] next 24 bytes Q[6] remaining length - R[6] status */
	REQ_SET_SENS,			/* set client sensitivity:	Q[0] float - R[6] status */
	REQ_GET_SENS,			/* get client sensitivity:	R[0] float R[6] status */
	REQ_SET_EVMASK,			/* set event mask: Q[0] mask - R[6] status */
	REQ_GET_EVMASK,			/* get event mask: R[0] mask R[6] status */

	/* device queries */
	REQ_DEV_NAME = 0x2000,	/* get device name:	R[0-5] next 24 bytes R[6] remaining length or -1 for failure */
	REQ_DEV_PATH,			/* get device path: same as above */
	REQ_DEV_NAXES,			/* get number of axes:	R[0] num axes R[6] status */
	REQ_DEV_NBUTTONS,		/* get number of buttons: same as above */
	REQ_DEV_USBID,			/* get USB id:			R[0] vend R[1] prod R[6] status */
	REQ_DEV_TYPE,			/* get device type:		R[0] type enum R[6] status */
	/* TODO: features like LCD, LEDs ... */

	/* configuration settings */
	REQ_SCFG_SENS = 0x3000,	/* set global sensitivity:	Q[0] float - R[6] status */
	REQ_GCFG_SENS,			/* get global sens:			R[0] float R[6] status */
	REQ_SCFG_SENS_AXIS,		/* set per-axis sens/ty:	Q[0-5] values - R[6] status */
	REQ_GCFG_SENS_AXIS,		/* get per-axis sens/ty:	R[0-5] values R[6] status */
	REQ_SCFG_DEADZONE,		/* set deadzones:			Q[0] dev axis Q[1] deadzone - R[6] status */
	REQ_GCFG_DEADZONE,		/* get deadzones:			R[0] dev axis - R[0] dev axis R[1] deadzone R[6] status */
	REQ_SCFG_INVERT,		/* set invert axes:			Q[0-5] invert - R[6] status */
	REQ_GCFG_INVERT,		/* get invert axes:			R[0-5] invert R[6] status */
	REQ_SCFG_AXISMAP,		/* set axis mapping:        Q[0] dev axis Q[1] mapping - R[6] status */
	REQ_GCFG_AXISMAP,		/* get axis mapping:		Q[0] dev axis - R[0] dev axis R[1] mapping R[6] status */
	REQ_SCFG_BNMAP,			/* set button mapping:		Q[0] dev bidx Q[1] map bidx - R[6] status */
	REQ_GCFG_BNMAP,			/* get button mapping:		Q[0] dev bidx - R[0] dev bidx R[1] map bidx R[6] status */
	REQ_SCFG_BNACTION,		/* set button action:		Q[0] bidx Q[1] action - R[6] status */
	REQ_GCFG_BNACTION,		/* get button action:		Q[0] bidx - R[0] bidx R[1] action R[6] status */
	REQ_SCFG_KBMAP,			/* set keyboard mapping:	Q[0] bidx Q[1] keysym - R[6] status */
	REQ_GCFG_KBMAP,			/* get keyboard mapping:	Q[0] bidx - R[0] bidx R[1] keysym R[6] status */
	REQ_SCFG_SWAPYZ,		/* set Y-Z axis swap:		Q[0] swap - R[6] status */
	REQ_GCFG_SWAPYZ,		/* get Y-Z axis swap:		R[0] swap R[6] status */
	REQ_SCFG_LED,			/* set LED state:			Q[0] state - R[6] status */
	REQ_GCFG_LED,			/* get LED state:			R[0] state R[6] status */
	REQ_SCFG_GRAB,			/* set device grabbing:		Q[0] state - R[6] status */
	REQ_GCFG_GRAB,			/* get device grabbing:		R[0] state R[6] status */
	REQ_SCFG_SERDEV,		/* set serial device path:	Q[0-5] next 24 bytes Q[6] remaining length - R[6] status */
	REQ_GCFG_SERDEV,		/* get serial device path:	R[0-5] next 24 bytes R[6] remaining length or -1 for failure */
	REQ_SCFG_REPEAT,		/* set repeat interval:		Q[0] interval (msec) - R[6] status */
	REQ_GCFG_REPEAT,		/* get repeat interval:		R[0] interval (msec) R[6] status */
	/* TODO ... more */
	REQ_CFG_SAVE = 0x3ffe,	/* save config file:        R[6] status */
	REQ_CFG_RESTORE,		/* load config from file:   R[6] status */
	REQ_CFG_RESET,			/* reset to default config: R[6] status */

	REQ_CHANGE_PROTO	= 0x5500
};

/* XXX keep in sync with SPNAV_DEV_* in spnav.h (libspnav) */
enum {
	DEV_UNKNOWN,
	/* serial devices */
	DEV_SB2003 = 0x100,	/* Spaceball 1003/2003/2003C */
	DEV_SB3003,			/* Spaceball 3003/3003C */
	DEV_SB4000,			/* Spaceball 4000FLX/5000FLX */
	DEV_SM,				/* Magellan SpaceMouse */
	DEV_SM5000,			/* Spaceball 5000 (spacemouse protocol) */
	DEV_SMCADMAN,		/* 3Dconnexion CadMan (spacemouse protocol) */
	/* USB devices */
	DEV_PLUSXT = 0x200,	/* SpaceMouse Plus XT */
	DEV_CADMAN,			/* 3Dconnexion CadMan (USB version) */
	DEV_SMCLASSIC,		/* SpaceMouse Classic */
	DEV_SB5000,			/* Spaceball 5000 (USB version) */
	DEV_STRAVEL,		/* Space Traveller */
	DEV_SPILOT,			/* Space Pilot */
	DEV_SNAV,			/* Space Navigator */
	DEV_SEXP,			/* Space Explorer */
	DEV_SNAVNB,			/* Space Navigator for Notebooks */
	DEV_SPILOTPRO,		/* Space Pilot pro */
	DEV_SMPRO,			/* SpaceMouse Pro */
	DEV_NULOOQ,			/* Nulooq */
	DEV_SMW,			/* SpaceMouse Wireless */
	DEV_SMPROW,			/* SpaceMouse Pro Wireless */
	DEV_SMENT,			/* SpaceMouse Enterprise */
	DEV_SMCOMP,			/* SpaceMouse Compact */
	DEV_SMMOD			/* SpaceMouse Module */
};


#define REQSTR_CHUNK_SIZE	24
#define REQSTR_CONT_BIT		0x10000
#define REQSTR_FIRST(rr)	(((rr)->data[6] & REQSTR_CONT_BIT) == 0)
#define REQSTR_REMLEN(rr)	((rr)->data[6] & 0xffff)

int spnav_send_str(int fd, int req, const char *str);
int spnav_recv_str(struct reqresp_strbuf *sbuf, struct reqresp *rr);

#ifdef DEF_PROTO_REQ_NAMES
const char *spnav_reqnames_1000[] = {
	"SET_NAME",
	"SET_SENS",
	"GET_SENS",
	"SET_EVMASK",
	"GET_EVMASK"
};
const char *spnav_reqnames_2000[] = {
	"DEV_NAME",
	"DEV_PATH",
	"DEV_NAXES",
	"DEV_NBUTTONS",
	"DEV_USBID",
	"DEV_TYPE"
};
const char *spnav_reqnames_3000[] = {
	"SCFG_SENS",
	"GCFG_SENS",
	"SCFG_SENS_AXIS",
	"GCFG_SENS_AXIS",
	"SCFG_DEADZONE",
	"GCFG_DEADZONE",
	"SCFG_INVERT",
	"GCFG_INVERT",
	"SCFG_AXISMAP",
	"GCFG_AXISMAP",
	"SCFG_BNMAP",
	"GCFG_BNMAP",
	"SCFG_BNACTION",
	"GCFG_BNACTION",
	"SCFG_KBMAP",
	"GCFG_KBMAP",
	"SCFG_SWAPYZ",
	"GCFG_SWAPYZ",
	"SCFG_LED",
	"GCFG_LED",
	"SCFG_GRAB",
	"GCFG_GRAB",
	"SCFG_SERDEV",
	"GCFG_SERDEV",
	"SCFG_REPEAT",
	"GCFG_REPEAT"
};

const int spnav_reqnames_1000_size = sizeof spnav_reqnames_1000 / sizeof *spnav_reqnames_1000;
const int spnav_reqnames_2000_size = sizeof spnav_reqnames_2000 / sizeof *spnav_reqnames_2000;
const int spnav_reqnames_3000_size = sizeof spnav_reqnames_3000 / sizeof *spnav_reqnames_3000;
#else
extern const char *spnav_reqnames_1000[];
extern const char *spnav_reqnames_2000[];
extern const char *spnav_reqnames_3000[];
extern const int spnav_reqnames_1000_size;
extern const int spnav_reqnames_2000_size;
extern const int spnav_reqnames_3000_size;
#endif	/* DEF_PROTO_REQ_NAMES */

#endif	/* PROTO_H_ */
