/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_IOCTL_H
#define OSV_IOCTL_H

#include <sys/ioctl.h>

#define _IOC_TYPE(x) (((x) >> 24) & 0xff)

#define SIOCBEGIN   0x8900
#define SIOCEND     0x89ff

/*
 * Undef unsupported Linux ioctls.
 */
#undef SIOCADDRT
#undef SIOCDELRT
#undef SIOCRTMSG
#undef SIOCGIFNAME
#undef SIOCSIFLINK
#undef SIOCGIFMEM
#undef SIOCSIFMEM
#undef SIOCSIFHWADDR
#undef SIOCGIFENCAP
#undef SIOCSIFENCAP
#undef SIOCGIFSLAVE
#undef SIOCSIFSLAVE
#undef SIOCSIFPFLAsysGS
#undef SIOCGIFPFLAGS
#undef SIOCSIFHWBROADCAST
#undef SIOCGIFCOUNT
#undef SIOCGIFTXQLEN
#undef SIOCSIFTXQLEN
#undef SIOCDARP
#undef SIOCGARP
#undef SIOCSARP
#undef SIOCDRARP
#undef SIOCGRARP
#undef SIOCSRARP
#undef SIOCGIFMAP
#undef SIOCSIFMAP
#undef SIOCADDDLCI
#undef SIOCDELDLCI
#undef SIOCDEVPRIVATE
#undef SIOCPROTOPRIVATE

/* Generic file-descriptor ioctl's. */
#define	FIONWRITE	_IOR('f', 119, int)	/* get # bytes (yet) to write */
#define	FIONSPACE	_IOR('f', 118, int)	/* get space in send queue */

/* Socket ioctl's. */

#define	SIOCAIFADDR	 _IOW('i', 26, struct ifaliasreq)/* add/chg IF alias */
#define	SIOCALIFADDR	 _IOW('i', 27, struct if_laddrreq) /* add IF addr */
#define	SIOCGLIFADDR	_IOWR('i', 28, struct if_laddrreq) /* get IF addr */
#define	SIOCDLIFADDR	 _IOW('i', 29, struct if_laddrreq) /* delete IF addr */
#define	SIOCSIFCAP	 _IOW('i', 30, struct bsd_ifreq)	/* set IF features */
#define	SIOCGIFCAP	_IOWR('i', 31, struct bsd_ifreq)	/* get IF features */
#define	SIOCGIFMAC	_IOWR('i', 38, struct bsd_ifreq)	/* get IF MAC label */
#define	SIOCSIFMAC	 _IOW('i', 39, struct bsd_ifreq)	/* set IF MAC label */
#define	SIOCSIFDESCR	 _IOW('i', 41, struct bsd_ifreq)	/* set ifnet descr */
#define	SIOCGIFDESCR	_IOWR('i', 42, struct bsd_ifreq)	/* get ifnet descr */

#define	SIOCGIFPHYS	_IOWR('i', 53, struct bsd_ifreq)	/* get IF wire */
#define	SIOCSIFPHYS	 _IOW('i', 54, struct bsd_ifreq)	/* set IF wire */
#define	SIOCSIFMEDIA	_IOWR('i', 55, struct bsd_ifreq)	/* set net media */
#define	SIOCGIFMEDIA	_IOWR('i', 56, struct ifmediareq) /* get net media */
#define	OSIOCGIFCONF	_IOWR('i', 20, struct bsd_ifconf)	/* get ifnet list */

#define	SIOCSIFGENERIC	 _IOW('i', 57, struct bsd_ifreq)	/* generic IF set op */
#define	SIOCGIFGENERIC	_IOWR('i', 58, struct bsd_ifreq)	/* generic IF get op */

#define	SIOCGIFSTATUS	_IOWR('i', 59, struct ifstat)	/* get IF status */
#define	SIOCSIFLLADDR	 _IOW('i', 60, struct bsd_ifreq)	/* set linklevel addr */

#define	SIOCSIFPHYADDR	 _IOW('i', 70, struct ifaliasreq) /* set gif addres */
#define	SIOCGIFPSRCADDR	_IOWR('i', 71, struct bsd_ifreq)	/* get gif psrc addr */
#define	SIOCGIFPDSTADDR	_IOWR('i', 72, struct bsd_ifreq)	/* get gif pdst addr */
#define	SIOCDIFPHYADDR	 _IOW('i', 73, struct bsd_ifreq)	/* delete gif addrs */
#define	SIOCSLIFPHYADDR	 _IOW('i', 74, struct if_laddrreq) /* set gif addrs */
#define	SIOCGLIFPHYADDR	_IOWR('i', 75, struct if_laddrreq) /* get gif addrs */

#define	SIOCGIFFIB	_IOWR('i', 92, struct bsd_ifreq)	/* get IF fib */
#define	SIOCSIFFIB	 _IOW('i', 93, struct bsd_ifreq)	/* set IF fib */

#define	SIOCIFCREATE	_IOWR('i', 122, struct bsd_ifreq)	/* create clone if */
#define	SIOCIFCREATE2	_IOWR('i', 124, struct bsd_ifreq)	/* create clone if */
#define	SIOCIFDESTROY	 _IOW('i', 121, struct bsd_ifreq)	/* destroy clone if */
#define	SIOCIFGCLONERS	_IOWR('i', 120, struct if_clonereq) /* get cloners */

#define	SIOCAIFGROUP	 _IOW('i', 135, struct ifgroupreq) /* add an ifgroup */
#define	SIOCGIFGROUP	_IOWR('i', 136, struct ifgroupreq) /* get ifgroups */
#define	SIOCDIFGROUP	 _IOW('i', 137, struct ifgroupreq) /* delete ifgroup */
#define	SIOCGIFGMEMB	_IOWR('i', 138, struct ifgroupreq) /* get members */

#endif /* !OSV_IOCTL_H */
