/*-
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)in.h	8.3 (Berkeley) 1/3/94
 * $FreeBSD$
 */

#ifndef _NETINET_IN_H_
#define	_NETINET_IN_H_

#define __NEED_in_addr_t
#define __NEED_in_port_t
#define __NEED_uint8_t
#define __NEED_uint16_t
#define __NEED_uint32_t
#define __NEED_socklen_t
#define __NEED_struct_in_addr
#include <bits/alltypes.h>

#include <sys/cdefs.h>
#include <bsd/machine/endian.h>


#define	INADDR_ANY		(u_int32_t)0x00000000
#define	INADDR_BROADCAST	(u_int32_t)0xffffffff	/* must be masked */

#include <bsd/sys/sys/_sockaddr_storage.h>

/* Socket address, internet style. */
struct bsd_sockaddr_in {
	uint8_t	sin_len;
	bsd_sa_family_t	sin_family;
	in_port_t	sin_port;
	struct	in_addr sin_addr;
	char	sin_zero[8];
};

#if !defined(_KERNEL) && __BSD_VISIBLE

#ifndef _BYTEORDER_PROTOTYPED
#define	_BYTEORDER_PROTOTYPED
__BEGIN_DECLS
uint32_t	htonl(uint32_t);
uint16_t	htons(uint16_t);
uint32_t	ntohl(uint32_t);
uint16_t	ntohs(uint16_t);
__END_DECLS
#endif

#ifndef _BYTEORDER_FUNC_DEFINED
#define	_BYTEORDER_FUNC_DEFINED
#define	htonl(x)	__htonl(x)
#define	htons(x)	__htons(x)
#define	ntohl(x)	__ntohl(x)
#define	ntohs(x)	__ntohs(x)
#endif

#endif /* !_KERNEL && __BSD_VISIBLE */

#define	IPPROTO_RAW		255		/* raw IP packet */
#define	INET_ADDRSTRLEN		16

#if __BSD_VISIBLE

/* last return value of *_input(), meaning "all job for this pkt is done".  */
#define	IPPROTO_DONE		257

/* Only used internally, so can be outside the range of valid IP protocols. */
#define	IPPROTO_DIVERT		258		/* divert pseudo-protocol */
#define	IPPROTO_SEND		259		/* SeND pseudo-protocol */

/*
 * Defined to avoid confusion.  The master value is defined by
 * PROTO_SPACER in sys/protosw.h.
 */
#define	IPPROTO_SPACER		32767		/* spacer for loadable protos */

/*
 * Local port number conventions:
 *
 * When a user does a bind(2) or connect(2) with a port number of zero,
 * a non-conflicting local port address is chosen.
 * The default range is IPPORT_HIFIRSTAUTO through
 * IPPORT_HILASTAUTO, although that is settable by sysctl.
 *
 * A user may set the IPPROTO_IP option IP_PORTRANGE to change this
 * default assignment range.
 *
 * The value IP_PORTRANGE_DEFAULT causes the default behavior.
 *
 * The value IP_PORTRANGE_HIGH changes the range of candidate port numbers
 * into the "high" range.  These are reserved for client outbound connections
 * which do not want to be filtered by any firewalls.
 *
 * The value IP_PORTRANGE_LOW changes the range to the "low" are
 * that is (by convention) restricted to privileged processes.  This
 * convention is based on "vouchsafe" principles only.  It is only secure
 * if you trust the remote host to restrict these ports.
 *
 * The default range of ports and the high range can be changed by
 * sysctl(3).  (net.inet.ip.port{hi,low}{first,last}_auto)
 *
 * Changing those values has bad security implications if you are
 * using a stateless firewall that is allowing packets outside of that
 * range in order to allow transparent outgoing connections.
 *
 * Such a firewall configuration will generally depend on the use of these
 * default values.  If you change them, you may find your Security
 * Administrator looking for you with a heavy object.
 *
 * For a slightly more orthodox text view on this:
 *
 *            ftp://ftp.isi.edu/in-notes/iana/assignments/port-numbers
 *
 *    port numbers are divided into three ranges:
 *
 *                0 -  1023 Well Known Ports
 *             1024 - 49151 Registered Ports
 *            49152 - 65535 Dynamic and/or Private Ports
 *
 */

/*
 * Ports < IPPORT_RESERVED are reserved for
 * privileged processes (e.g. root).         (IP_PORTRANGE_LOW)
 */
#define	IPPORT_RESERVED		1024

/*
 * Default local port range, used by IP_PORTRANGE_DEFAULT
 */
#define IPPORT_EPHEMERALFIRST	10000
#define IPPORT_EPHEMERALLAST	65535 
 
/*
 * Dynamic port range, used by IP_PORTRANGE_HIGH.
 */
#define	IPPORT_HIFIRSTAUTO	49152
#define	IPPORT_HILASTAUTO	65535

/*
 * Scanning for a free reserved port return a value below IPPORT_RESERVED,
 * but higher than IPPORT_RESERVEDSTART.  Traditionally the start value was
 * 512, but that conflicts with some well-known-services that firewalls may
 * have a fit if we use.
 */
#define	IPPORT_RESERVEDSTART	600

#define	IPPORT_MAX		65535

/*
 * Definitions of bits in internet address integers.
 * On subnets, the decomposition of addresses to host and net parts
 * is done according to subnet mask, not the masks here.
 */
#define	IN_CLASSA(i)		(((u_int32_t)(i) & 0x80000000) == 0)
#define	IN_CLASSA_NET		0xff000000
#define	IN_CLASSA_NSHIFT	24
#define	IN_CLASSA_HOST		0x00ffffff
#define	IN_CLASSA_MAX		128

#define	IN_CLASSB(i)		(((u_int32_t)(i) & 0xc0000000) == 0x80000000)
#define	IN_CLASSB_NET		0xffff0000
#define	IN_CLASSB_NSHIFT	16
#define	IN_CLASSB_HOST		0x0000ffff
#define	IN_CLASSB_MAX		65536

#define	IN_CLASSC(i)		(((u_int32_t)(i) & 0xe0000000) == 0xc0000000)
#define	IN_CLASSC_NET		0xffffff00
#define	IN_CLASSC_NSHIFT	8
#define	IN_CLASSC_HOST		0x000000ff

#define	IN_CLASSD(i)		(((u_int32_t)(i) & 0xf0000000) == 0xe0000000)
#define	IN_CLASSD_NET		0xf0000000	/* These ones aren't really */
#define	IN_CLASSD_NSHIFT	28		/* net and host fields, but */
#define	IN_CLASSD_HOST		0x0fffffff	/* routing needn't know.    */
#define	IN_MULTICAST(i)		IN_CLASSD(i)

#define	IN_EXPERIMENTAL(i)	(((u_int32_t)(i) & 0xf0000000) == 0xf0000000)
#define	IN_BADCLASS(i)		(((u_int32_t)(i) & 0xf0000000) == 0xf0000000)

#define IN_LINKLOCAL(i)		(((u_int32_t)(i) & 0xffff0000) == 0xa9fe0000)
#define IN_LOOPBACK(i)		(((u_int32_t)(i) & 0xff000000) == 0x7f000000)
#define IN_ZERONET(i)		(((u_int32_t)(i) & 0xff000000) == 0)

#define	IN_PRIVATE(i)	((((u_int32_t)(i) & 0xff000000) == 0x0a000000) || \
			 (((u_int32_t)(i) & 0xfff00000) == 0xac100000) || \
			 (((u_int32_t)(i) & 0xffff0000) == 0xc0a80000))

#define	IN_LOCAL_GROUP(i)	(((u_int32_t)(i) & 0xffffff00) == 0xe0000000)
 
#define	IN_ANY_LOCAL(i)		(IN_LINKLOCAL(i) || IN_LOCAL_GROUP(i))

#define	INADDR_LOOPBACK		(u_int32_t)0x7f000001
#ifndef _KERNEL
#define	INADDR_NONE		0xffffffff		/* -1 return */
#endif

#define	INADDR_UNSPEC_GROUP	(u_int32_t)0xe0000000	/* 224.0.0.0 */
#define	INADDR_ALLHOSTS_GROUP	(u_int32_t)0xe0000001	/* 224.0.0.1 */
#define	INADDR_ALLRTRS_GROUP	(u_int32_t)0xe0000002	/* 224.0.0.2 */
#define	INADDR_ALLRPTS_GROUP	(u_int32_t)0xe0000016	/* 224.0.0.22, IGMPv3 */
#define	INADDR_CARP_GROUP	(u_int32_t)0xe0000012	/* 224.0.0.18 */
#define	INADDR_PFSYNC_GROUP	(u_int32_t)0xe00000f0	/* 224.0.0.240 */
#define	INADDR_ALLMDNS_GROUP	(u_int32_t)0xe00000fb	/* 224.0.0.251 */
#define	INADDR_MAX_LOCAL_GROUP	(u_int32_t)0xe00000ff	/* 224.0.0.255 */

#define	IN_LOOPBACKNET		127			/* official! */

#define	IN_RFC3021_MASK		(u_int32_t)0xfffffffe

/*
 * Options for use with [gs]etsockopt at the IP level.
 * First word of comment is data type; bool is stored in int.
 */
#define	IP_OPTIONS		1    /* buf/ip_opts; set/get IP options */
#define	IP_HDRINCL		2    /* int; header is included with data */
#define	IP_TOS			3    /* int; IP type of service and preced. */
#define	IP_TTL			4    /* int; IP time to live */
#define	IP_RECVOPTS		5    /* bool; receive all IP opts w/dgram */
#define	IP_RECVRETOPTS		6    /* bool; receive IP opts for response */
#define	IP_RECVDSTADDR		7    /* bool; receive IP dst addr w/dgram */
#define	IP_SENDSRCADDR		IP_RECVDSTADDR /* cmsg_type to set src addr */
#define	IP_RETOPTS		8    /* ip_opts; set/get IP options */
#define	IP_MULTICAST_IF		9    /* struct in_addr *or* struct ip_mreqn;
				      * set/get IP multicast i/f  */
#define	IP_MULTICAST_TTL	10   /* u_char; set/get IP multicast ttl */
#define	IP_MULTICAST_LOOP	11   /* u_char; set/get IP multicast loopback */
#define	IP_ADD_MEMBERSHIP	12   /* ip_mreq; add an IP group membership */
#define	IP_DROP_MEMBERSHIP	13   /* ip_mreq; drop an IP group membership */
#define	IP_MULTICAST_VIF	14   /* set/get IP mcast virt. iface */
#define	IP_RSVP_ON		15   /* enable RSVP in kernel */
#define	IP_RSVP_OFF		16   /* disable RSVP in kernel */
#define	IP_RSVP_VIF_ON		17   /* set RSVP per-vif socket */
#define	IP_RSVP_VIF_OFF		18   /* unset RSVP per-vif socket */
#define	IP_PORTRANGE		19   /* int; range to choose for unspec port */
#define	IP_RECVIF		20   /* bool; receive reception if w/dgram */
/* for IPSEC */
#define	IP_IPSEC_POLICY		21   /* int; set/get security policy */
#define	IP_FAITH		22   /* bool; accept FAITH'ed connections */

#define	IP_ONESBCAST		23   /* bool: send all-ones broadcast */
#define	IP_BINDANY		24   /* bool: allow bind to any address */

/*
 * Options for controlling the firewall and dummynet.
 * Historical options (from 40 to 64) will eventually be
 * replaced by only two options, IP_FW3 and IP_DUMMYNET3.
 */
#define	IP_FW_TABLE_ADD		40   /* add entry */
#define	IP_FW_TABLE_DEL		41   /* delete entry */
#define	IP_FW_TABLE_FLUSH	42   /* flush table */
#define	IP_FW_TABLE_GETSIZE	43   /* get table size */
#define	IP_FW_TABLE_LIST	44   /* list table contents */

#define	IP_FW3			48   /* generic ipfw v.3 sockopts */
#define	IP_DUMMYNET3		49   /* generic dummynet v.3 sockopts */

#define	IP_FW_ADD		50   /* add a firewall rule to chain */
#define	IP_FW_DEL		51   /* delete a firewall rule from chain */
#define	IP_FW_FLUSH		52   /* flush firewall rule chain */
#define	IP_FW_ZERO		53   /* clear single/all firewall counter(s) */
#define	IP_FW_GET		54   /* get entire firewall rule chain */
#define	IP_FW_RESETLOG		55   /* reset logging counters */

#define IP_FW_NAT_CFG           56   /* add/config a nat rule */
#define IP_FW_NAT_DEL           57   /* delete a nat rule */
#define IP_FW_NAT_GET_CONFIG    58   /* get configuration of a nat rule */
#define IP_FW_NAT_GET_LOG       59   /* get log of a nat rule */

#define	IP_DUMMYNET_CONFIGURE	60   /* add/configure a dummynet pipe */
#define	IP_DUMMYNET_DEL		61   /* delete a dummynet pipe from chain */
#define	IP_DUMMYNET_FLUSH	62   /* flush dummynet */
#define	IP_DUMMYNET_GET		64   /* get entire dummynet pipes */

#define	IP_RECVTTL		65   /* bool; receive IP TTL w/dgram */
#define	IP_MINTTL		66   /* minimum TTL for packet or drop */
#define	IP_DONTFRAG		67   /* don't fragment packet */
#define	IP_RECVTOS		68   /* bool; receive IP TOS w/dgram */

/* IPv4 Source Filter Multicast API [RFC3678] */
#define	IP_ADD_SOURCE_MEMBERSHIP	70   /* join a source-specific group */
#define	IP_DROP_SOURCE_MEMBERSHIP	71   /* drop a single source */
#define	IP_BLOCK_SOURCE			72   /* block a source */
#define	IP_UNBLOCK_SOURCE		73   /* unblock a source */

/* The following option is private; do not use it from user applications. */
#define	IP_MSFILTER			74   /* set/get filter list */

/* Protocol Independent Multicast API [RFC3678] */
#define	MCAST_JOIN_GROUP		80   /* join an any-source group */
#define	MCAST_LEAVE_GROUP		81   /* leave all sources for group */
#define	MCAST_JOIN_SOURCE_GROUP		82   /* join a source-specific group */
#define	MCAST_LEAVE_SOURCE_GROUP	83   /* leave a single source */
#define	MCAST_BLOCK_SOURCE		84   /* block a source */
#define	MCAST_UNBLOCK_SOURCE		85   /* unblock a source */

/*
 * Defaults and limits for options
 */
#define	IP_DEFAULT_MULTICAST_TTL  1	/* normally limit m'casts to 1 hop  */
#define	IP_DEFAULT_MULTICAST_LOOP 1	/* normally hear sends if a member  */

/*
 * The imo_membership vector for each socket is now dynamically allocated at
 * run-time, bounded by USHRT_MAX, and is reallocated when needed, sized
 * according to a power-of-two increment.
 */
#define	IP_MIN_MEMBERSHIPS	31
#define	IP_MAX_MEMBERSHIPS	4095
#define	IP_MAX_SOURCE_FILTER	1024	/* XXX to be unused */

/*
 * Default resource limits for IPv4 multicast source filtering.
 * These may be modified by sysctl.
 */
#define	IP_MAX_GROUP_SRC_FILTER		512	/* sources per group */
#define	IP_MAX_SOCK_SRC_FILTER		128	/* sources per socket/group */
#define	IP_MAX_SOCK_MUTE_FILTER		128	/* XXX no longer used */

#include <netinet/__in.h>

/*
 * Argument structure for IPv4 Multicast Source Filter APIs. [RFC3678]
 */
struct ip_mreq_source {
	struct	in_addr imr_multiaddr;	/* IP multicast address of group */
	struct	in_addr imr_sourceaddr;	/* IP address of source */
	struct	in_addr imr_interface;	/* local IP address of interface */
};

/*
 * Argument structures for Protocol-Independent Multicast Source
 * Filter APIs. [RFC3678]
 */
struct group_req {
	uint32_t		gr_interface;	/* interface index */
	struct bsd_sockaddr_storage	gr_group;	/* group address */
};

struct group_source_req {
	uint32_t		gsr_interface;	/* interface index */
	struct bsd_sockaddr_storage	gsr_group;	/* group address */
	struct bsd_sockaddr_storage	gsr_source;	/* source address */
};

#ifndef __MSFILTERREQ_DEFINED
#define __MSFILTERREQ_DEFINED
/*
 * The following structure is private; do not use it from user applications.
 * It is used to communicate IP_MSFILTER/IPV6_MSFILTER information between
 * the RFC 3678 libc functions and the kernel.
 */
struct __msfilterreq {
	uint32_t		 msfr_ifindex;	/* interface index */
	uint32_t		 msfr_fmode;	/* filter mode for group */
	uint32_t		 msfr_nsrcs;	/* # of sources in msfr_srcs */
	struct bsd_sockaddr_storage	 msfr_group;	/* group address */
	struct bsd_sockaddr_storage	*msfr_srcs;	/* pointer to the first member
						 * of a contiguous array of
						 * sources to filter in full.
						 */
};
#endif

struct bsd_sockaddr;

/*
 * Advanced (Full-state) APIs [RFC3678]
 * The RFC specifies uint_t for the 6th argument to [sg]etsourcefilter().
 * We use uint32_t here to be consistent.
 */
int	setipv4sourcefilter(int, struct in_addr, struct in_addr, uint32_t,
	    uint32_t, struct in_addr *);
int	getipv4sourcefilter(int, struct in_addr, struct in_addr, uint32_t *,
	    uint32_t *, struct in_addr *);
int	setsourcefilter(int, uint32_t, struct bsd_sockaddr *, socklen_t,
	    uint32_t, uint32_t, struct bsd_sockaddr_storage *);
int	getsourcefilter(int, uint32_t, struct bsd_sockaddr *, socklen_t,
	    uint32_t *, uint32_t *, struct bsd_sockaddr_storage *);

/*
 * Filter modes; also used to represent per-socket filter mode internally.
 */
#define	MCAST_UNDEFINED	0	/* fmode: not yet defined */
#define	MCAST_INCLUDE	1	/* fmode: include these source(s) */
#define	MCAST_EXCLUDE	2	/* fmode: exclude these source(s) */

/*
 * Argument for IP_PORTRANGE:
 * - which range to search when port is unspecified at bind() or connect()
 */
#define	IP_PORTRANGE_DEFAULT	0	/* default range */
#define	IP_PORTRANGE_HIGH	1	/* "high" - request firewall bypass */
#define	IP_PORTRANGE_LOW	2	/* "low" - vouchsafe security */

/*
 * Definitions for inet sysctl operations.
 *
 * Third level is protocol number.
 * Fourth level is desired variable within that protocol.
 */
#define	IPPROTO_MAXID	(IPPROTO_AH + 1)	/* don't list to IPPROTO_MAX */

#define	CTL_IPPROTO_NAMES { \
	{ "ip", CTLTYPE_NODE }, \
	{ "icmp", CTLTYPE_NODE }, \
	{ "igmp", CTLTYPE_NODE }, \
	{ "ggp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "tcp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ "egp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "pup", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "udp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "idp", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "ipsec", CTLTYPE_NODE }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "pim", CTLTYPE_NODE }, \
}

/*
 * Names for IP sysctl objects
 */
#define	IPCTL_FORWARDING	1	/* act as router */
#define	IPCTL_SENDREDIRECTS	2	/* may send redirects when forwarding */
#define	IPCTL_DEFTTL		3	/* default TTL */
#ifdef notyet
#define	IPCTL_DEFMTU		4	/* default MTU */
#endif
#define	IPCTL_RTEXPIRE		5	/* cloned route expiration time */
#define	IPCTL_RTMINEXPIRE	6	/* min value for expiration time */
#define	IPCTL_RTMAXCACHE	7	/* trigger level for dynamic expire */
#define	IPCTL_SOURCEROUTE	8	/* may perform source routes */
#define	IPCTL_DIRECTEDBROADCAST	9	/* may re-broadcast received packets */
#define	IPCTL_INTRQMAXLEN	10	/* max length of netisr queue */
#define	IPCTL_INTRQDROPS	11	/* number of netisr q drops */
#define	IPCTL_STATS		12	/* ipstat structure */
#define	IPCTL_ACCEPTSOURCEROUTE	13	/* may accept source routed packets */
#define	IPCTL_FASTFORWARDING	14	/* use fast IP forwarding code */
#define	IPCTL_KEEPFAITH		15	/* FAITH IPv4->IPv6 translater ctl */
#define	IPCTL_GIF_TTL		16	/* default TTL for gif encap packet */
#define	IPCTL_MAXID		17

#define	IPCTL_NAMES { \
	{ 0, 0 }, \
	{ "forwarding", CTLTYPE_INT }, \
	{ "redirect", CTLTYPE_INT }, \
	{ "ttl", CTLTYPE_INT }, \
	{ "mtu", CTLTYPE_INT }, \
	{ "rtexpire", CTLTYPE_INT }, \
	{ "rtminexpire", CTLTYPE_INT }, \
	{ "rtmaxcache", CTLTYPE_INT }, \
	{ "sourceroute", CTLTYPE_INT }, \
	{ "directed-broadcast", CTLTYPE_INT }, \
	{ "intr-queue-maxlen", CTLTYPE_INT }, \
	{ "intr-queue-drops", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT }, \
	{ "accept_sourceroute", CTLTYPE_INT }, \
	{ "fastforwarding", CTLTYPE_INT }, \
}

#endif /* __BSD_VISIBLE */

#ifdef _KERNEL

struct ifnet; struct mbuf;	/* forward declarations for Standard C */
__BEGIN_DECLS
int	 in_broadcast(struct in_addr, struct ifnet *);
int	 in_canforward(struct in_addr);
int	 in_localaddr(struct in_addr);
int	 in_localip(struct in_addr);
int	 inet_aton(const char *, struct in_addr *); /* in libkern */
char	*inet_ntoa(struct in_addr); /* in libkern */
const char *inet_ntoa_r(struct in_addr ina, char *buf, socklen_t); /* in libkern */
const char *inet_ntop(int, const void *, char *, socklen_t); /* in libkern */
int	 inet_pton(int af, const char *, void *); /* in libkern */
void	 in_ifdetach(struct ifnet *);
__END_DECLS

#define	in_hosteq(s, t)	((s).s_addr == (t).s_addr)
#define	in_nullhost(x)	((x).s_addr == INADDR_ANY)
#define	in_allhosts(x)	((x).s_addr == htonl(INADDR_ALLHOSTS_GROUP))

#define	satosin(sa)	((struct bsd_sockaddr_in *)(sa))
#define	sintosa(sin)	((struct bsd_sockaddr *)(sin))
#define	ifatoia(ifa)	((struct in_ifaddr *)(ifa))

/*
 * Historically, BSD keeps ip_len and ip_off in host format
 * when doing layer 3 processing, and this often requires
 * to translate the format back and forth.
 * To make the process explicit, we define a couple of macros
 * that also take into account the fact that at some point
 * we may want to keep those fields always in net format.
 */

#if (BYTE_ORDER == BIG_ENDIAN) || defined(HAVE_NET_IPLEN)
#define SET_NET_IPLEN(p)	do {} while (0)
#define SET_HOST_IPLEN(p)	do {} while (0)
#else
#define SET_NET_IPLEN(p)	do {		\
	struct ip *h_ip = (p);			\
	h_ip->ip_len = htons(h_ip->ip_len);	\
	h_ip->ip_off = htons(h_ip->ip_off);	\
	} while (0)

#define SET_HOST_IPLEN(p)	do {		\
	struct ip *h_ip = (p);			\
	h_ip->ip_len = ntohs(h_ip->ip_len);	\
	h_ip->ip_off = ntohs(h_ip->ip_off);	\
	} while (0)
#endif /* !HAVE_NET_IPLEN */

#endif /* _KERNEL */

/* INET6 stuff */
#if __POSIX_VISIBLE >= 200112
#define	__KAME_NETINET_IN_H_INCLUDED_
#include <netinet6/in6.h>
#undef __KAME_NETINET_IN_H_INCLUDED_
#endif

#endif /* !_NETINET_IN_H_*/
