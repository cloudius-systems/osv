/*-
 * Copyright (c) 1980, 1986, 1993
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
 *	@(#)route.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _NET_ROUTE_H_
#define _NET_ROUTE_H_

#include <sys/cdefs.h>
#include <porting/sync_stub.h>

__BEGIN_DECLS
void rts_init(void);
void route_init(void);
void vnet_route_init(void);
__END_DECLS

/*
 * Kernel resident routing tables.
 *
 * The routing tables are initialized when interface addresses
 * are set by making entries for all directly connected interfaces.
 */

/*
 * A route consists of a destination address, a reference
 * to a routing entry, and a reference to an llentry.  
 * These are often held by protocols in their control
 * blocks, e.g. inpcb.
 */
struct route {
	struct	rtentry *ro_rt;
	struct	llentry *ro_lle;
	struct	in_ifaddr *ro_ia;
	int		ro_flags;
	struct	bsd_sockaddr ro_dst;
};

#define	RT_CACHING_CONTEXT	0x1	/* XXX: not used anywhere */
#define	RT_NORTREF		0x2	/* doesn't hold reference on ro_rt */

/*
 * These numbers are used by reliable protocols for determining
 * retransmission behavior and are included in the routing structure.
 */
struct rt_metrics_lite {
	u_long	rmx_mtu;	/* MTU for this path */
	u_long	rmx_expire;	/* lifetime for route, e.g. redirect */
	u_long	rmx_pksent;	/* packets sent using this route */
	u_long	rmx_weight;	/* absolute weight */ 
};

struct rt_metrics {
	u_long	rmx_locks;	/* Kernel must leave these values alone */
	u_long	rmx_mtu;	/* MTU for this path */
	u_long	rmx_hopcount;	/* max hops expected */
	u_long	rmx_expire;	/* lifetime for route, e.g. redirect */
	u_long	rmx_recvpipe;	/* inbound delay-bandwidth product */
	u_long	rmx_sendpipe;	/* outbound delay-bandwidth product */
	u_long	rmx_ssthresh;	/* outbound gateway buffer limit */
	u_long	rmx_rtt;	/* estimated round trip time */
	u_long	rmx_rttvar;	/* estimated rtt variance */
	u_long	rmx_pksent;	/* packets sent using this route */
	u_long	rmx_weight;	/* route weight */
	u_long	rmx_filler[3];	/* will be used for T/TCP later */
};

/*
 * rmx_rtt and rmx_rttvar are stored as microseconds;
 * RTTTOPRHZ(rtt) converts to a value suitable for use
 * by a protocol slowtimo counter.
 */
#define	RTM_RTTUNIT	1000000	/* units for rtt, rttvar, as units per sec */
#define	RTTTOPRHZ(r)	((r) / (RTM_RTTUNIT / PR_SLOWHZ))

#define	RT_DEFAULT_FIB	0	/* Explicitly mark fib=0 restricted cases */
extern u_int rt_numfibs;	/* number fo usable routing tables */
/*
 * XXX kernel function pointer `rt_output' is visible to applications.
 */
struct mbuf;

/*
 * We distinguish between routes to hosts and routes to networks,
 * preferring the former if available.  For each route we infer
 * the interface to use from the gateway address supplied when
 * the route was entered.  Routes that forward packets through
 * gateways are marked so that the output routines know to address the
 * gateway rather than the ultimate destination.
 */
#ifndef RNF_NORMAL
#include <bsd/sys/net/radix.h>
#ifdef RADIX_MPATH
#include <net/radix_mpath.h>
#endif
#endif
struct rtentry {
	struct	radix_node rt_nodes[2];	/* tree glue, and other values */
	/*
	 * XXX struct rtentry must begin with a struct radix_node (or two!)
	 * because the code does some casts of a 'struct radix_node *'
	 * to a 'struct rtentry *'
	 */
#define	rt_key(r)	(*((struct bsd_sockaddr **)(&(r)->rt_nodes->rn_key)))
#define	rt_mask(r)	(*((struct bsd_sockaddr **)(&(r)->rt_nodes->rn_mask)))
	struct	bsd_sockaddr *rt_gateway;	/* value */
	int	rt_flags;		/* up/down?, host/net */
	int	rt_refcnt;		/* # held references */
	struct	ifnet *rt_ifp;		/* the answer: interface to use */
	struct	bsd_ifaddr *rt_ifa;		/* the answer: interface address to use */
	struct	rt_metrics_lite rt_rmx;	/* metrics used by rx'ing protocols */
	u_int	rt_fibnum;		/* which FIB */
#ifdef _KERNEL
	/* XXX ugly, user apps use this definition but don't have a mtx def */
#if 0
	struct	mtx rt_mtx;		/* mutex for routing entry */
#endif
#endif
};

/*
 * Following structure necessary for 4.3 compatibility;
 * We should eventually move it to a compat file.
 */
struct ortentry {
	u_long	rt_hash;		/* to speed lookups */
	struct	bsd_sockaddr rt_dst;	/* key */
	struct	bsd_sockaddr rt_gateway;	/* value */
	short	rt_flags;		/* up/down?, host/net */
	short	rt_refcnt;		/* # held references */
	u_long	rt_use;			/* raw # packets forwarded */
	struct	ifnet *rt_ifp;		/* the answer: interface to use */
};

#define rt_use rt_rmx.rmx_pksent

#define	RTF_UP		0x1		/* route usable */
#define	RTF_GATEWAY	0x2		/* destination is a gateway */
#define	RTF_HOST	0x4		/* host entry (net otherwise) */
#define	RTF_REJECT	0x8		/* host or net unreachable */
#define	RTF_DYNAMIC	0x10		/* created dynamically (by redirect) */
#define	RTF_MODIFIED	0x20		/* modified dynamically (by redirect) */
#define RTF_DONE	0x40		/* message confirmed */
/*			0x80		   unused, was RTF_DELCLONE */
/*			0x100		   unused, was RTF_CLONING */
#define RTF_XRESOLVE	0x200		/* external daemon resolves name */
#define RTF_LLINFO	0x400		/* DEPRECATED - exists ONLY for backward 
					   compatibility */
#define RTF_LLDATA	0x400		/* used by apps to add/del L2 entries */
#define RTF_STATIC	0x800		/* manually added */
#define RTF_BLACKHOLE	0x1000		/* just discard pkts (during updates) */
#define RTF_PROTO2	0x4000		/* protocol specific routing flag */
#define RTF_PROTO1	0x8000		/* protocol specific routing flag */

/* XXX: temporary to stay API/ABI compatible with userland */
#ifndef _KERNEL
#define RTF_PRCLONING	0x10000		/* unused, for compatibility */
#endif

/*			0x20000		   unused, was RTF_WASCLONED */
#define RTF_PROTO3	0x40000		/* protocol specific routing flag */
/*			0x80000		   unused */
#define RTF_PINNED	0x100000	/* future use */
#define	RTF_LOCAL	0x200000 	/* route represents a local address */
#define	RTF_BROADCAST	0x400000	/* route represents a bcast address */
#define	RTF_MULTICAST	0x800000	/* route represents a mcast address */
					/* 0x8000000 and up unassigned */
#define	RTF_STICKY	 0x10000000	/* always route dst->src */

#define	RTF_RNH_LOCKED	 0x40000000	/* radix node head is locked */

/* Mask of RTF flags that are allowed to be modified by RTM_CHANGE. */
#define RTF_FMASK	\
	(RTF_PROTO1 | RTF_PROTO2 | RTF_PROTO3 | RTF_BLACKHOLE | \
	 RTF_REJECT | RTF_STATIC | RTF_STICKY)

/*
 * Routing statistics.
 */
struct	rtstat {
	short	rts_badredirect;	/* bogus redirect calls */
	short	rts_dynamic;		/* routes created by redirects */
	short	rts_newgateway;		/* routes modified by redirects */
	short	rts_unreach;		/* lookups which failed */
	short	rts_wildcard;		/* lookups satisfied by a wildcard */
};
/*
 * Structures for routing messages.
 */
struct rt_msghdr {
	u_short	rtm_msglen;	/* to skip over non-understood messages */
	u_char	rtm_version;	/* future binary compatibility */
	u_char	rtm_type;	/* message type */
	u_short	rtm_index;	/* index for associated ifp */
	int	rtm_flags;	/* flags, incl. kern & message, e.g. DONE */
	int	rtm_addrs;	/* bitmask identifying bsd_sockaddrs in msg */
	int	rtm_pid;	/* identify sender */
	int	rtm_seq;	/* for sender to identify action */
	int	rtm_errno;	/* why failed */
	int	rtm_fmask;	/* bitmask used in RTM_CHANGE message */
	u_long	rtm_inits;	/* which metrics we are initializing */
	struct	rt_metrics rtm_rmx; /* metrics themselves */
};

#define RTM_VERSION	5	/* Up the ante and ignore older versions */

/*
 * Message types.
 */
#define RTM_ADD		0x1	/* Add Route */
#define RTM_DELETE	0x2	/* Delete Route */
#define RTM_CHANGE	0x3	/* Change Metrics or flags */
#define RTM_GET		0x4	/* Report Metrics */
#define RTM_LOSING	0x5	/* Kernel Suspects Partitioning */
#define RTM_REDIRECT	0x6	/* Told to use different route */
#define RTM_MISS	0x7	/* Lookup failed on this address */
#define RTM_LOCK	0x8	/* fix specified metrics */
#define RTM_OLDADD	0x9	/* caused by SIOCADDRT */
#define RTM_OLDDEL	0xa	/* caused by SIOCDELRT */
#define RTM_RESOLVE	0xb	/* req to resolve dst to LL addr */
#define RTM_NEWADDR	0xc	/* address being added to iface */
#define RTM_DELADDR	0xd	/* address being removed from iface */
#define RTM_IFINFO	0xe	/* iface going up/down etc. */
#define	RTM_NEWMADDR	0xf	/* mcast group membership being added to if */
#define	RTM_DELMADDR	0x10	/* mcast group membership being deleted */
#define	RTM_IFANNOUNCE	0x11	/* iface arrival/departure */
#define	RTM_IEEE80211	0x12	/* IEEE80211 wireless event */

/*
 * Bitmask values for rtm_inits and rmx_locks.
 */
#define RTV_MTU		0x1	/* init or lock _mtu */
#define RTV_HOPCOUNT	0x2	/* init or lock _hopcount */
#define RTV_EXPIRE	0x4	/* init or lock _expire */
#define RTV_RPIPE	0x8	/* init or lock _recvpipe */
#define RTV_SPIPE	0x10	/* init or lock _sendpipe */
#define RTV_SSTHRESH	0x20	/* init or lock _ssthresh */
#define RTV_RTT		0x40	/* init or lock _rtt */
#define RTV_RTTVAR	0x80	/* init or lock _rttvar */
#define RTV_WEIGHT	0x100	/* init or lock _weight */

/*
 * Bitmask values for rtm_addrs.
 */
#define RTA_DST		0x1	/* destination bsd_sockaddr present */
#define RTA_GATEWAY	0x2	/* gateway bsd_sockaddr present */
#define RTA_NETMASK	0x4	/* netmask bsd_sockaddr present */
#define RTA_GENMASK	0x8	/* cloning mask bsd_sockaddr present */
#define RTA_IFP		0x10	/* interface name bsd_sockaddr present */
#define RTA_IFA		0x20	/* interface addr bsd_sockaddr present */
#define RTA_AUTHOR	0x40	/* bsd_sockaddr for author of redirect */
#define RTA_BRD		0x80	/* for NEWADDR, broadcast or p-p dest addr */

/*
 * Index offsets for bsd_sockaddr array for alternate internal encoding.
 */
#define RTAX_DST	0	/* destination bsd_sockaddr present */
#define RTAX_GATEWAY	1	/* gateway bsd_sockaddr present */
#define RTAX_NETMASK	2	/* netmask bsd_sockaddr present */
#define RTAX_GENMASK	3	/* cloning mask bsd_sockaddr present */
#define RTAX_IFP	4	/* interface name bsd_sockaddr present */
#define RTAX_IFA	5	/* interface addr bsd_sockaddr present */
#define RTAX_AUTHOR	6	/* bsd_sockaddr for author of redirect */
#define RTAX_BRD	7	/* for NEWADDR, broadcast or p-p dest addr */
#define RTAX_MAX	8	/* size of array to allocate */

struct rt_addrinfo {
	int	rti_addrs;
	struct	bsd_sockaddr *rti_info[RTAX_MAX];
	int	rti_flags;
	struct	bsd_ifaddr *rti_ifa;
	struct	ifnet *rti_ifp;
};

/*
 * This macro returns the size of a struct bsd_sockaddr when passed
 * through a routing socket. Basically we round up sa_len to
 * a multiple of sizeof(long), with a minimum of sizeof(long).
 * The check for a NULL pointer is just a convenience, probably never used.
 * The case sa_len == 0 should only apply to empty structures.
 */
#define SA_SIZE(sa)						\
    (  (!(sa) || ((struct bsd_sockaddr *)(sa))->sa_len == 0) ?	\
	sizeof(long)		:				\
	1 + ( (((struct bsd_sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1) ) )

#define SA_SIZE_ALWAYS(sa)                     \
    (  (((struct bsd_sockaddr *)(sa))->sa_len == 0) ?  \
    sizeof(long)        :               \
    1 + ( (((struct bsd_sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1) ) )

#ifdef _KERNEL

#define RT_LINK_IS_UP(ifp)	(!((ifp)->if_capabilities & IFCAP_LINKSTATE) \
				 || (ifp)->if_link_state == LINK_STATE_UP)

#if 1
#define	RT_LOCK_INIT(_rt)
#define	RT_LOCK(_rt)
#define	RT_UNLOCK(_rt)
#define	RT_LOCK_DESTROY(_rt)
#define	RT_LOCK_ASSERT(_rt)

#define	RT_ADDREF(_rt)	do {					\
} while (0)

#define	RT_REMREF(_rt)	do {					\
} while (0)

#define	RTFREE_LOCKED(_rt) do {					\
} while (0)

#define	RTFREE(_rt) do {					\
} while (0)

#define	RO_RTFREE(_ro) do {					\
} while (0)
#else
#define	RT_LOCK_INIT(_rt) \
	mtx_init(&(_rt)->rt_mtx, "rtentry", NULL, MTX_DEF | MTX_DUPOK)
#define	RT_LOCK(_rt)		mtx_lock(&(_rt)->rt_mtx)
#define	RT_UNLOCK(_rt)		mtx_unlock(&(_rt)->rt_mtx)
#define	RT_LOCK_DESTROY(_rt)	mtx_destroy(&(_rt)->rt_mtx)
#define	RT_LOCK_ASSERT(_rt)	mtx_assert(&(_rt)->rt_mtx, MA_OWNED)

#define	RT_ADDREF(_rt)	do {					\
	RT_LOCK_ASSERT(_rt);					\
	KASSERT((_rt)->rt_refcnt >= 0,				\
		("negative refcnt %d", (_rt)->rt_refcnt));	\
	(_rt)->rt_refcnt++;					\
} while (0)

#define	RT_REMREF(_rt)	do {					\
	RT_LOCK_ASSERT(_rt);					\
	KASSERT((_rt)->rt_refcnt > 0,				\
		("bogus refcnt %d", (_rt)->rt_refcnt));	\
	(_rt)->rt_refcnt--;					\
} while (0)

#define	RTFREE_LOCKED(_rt) do {					\
	if ((_rt)->rt_refcnt <= 1)				\
		rtfree(_rt);					\
	else {							\
		RT_REMREF(_rt);					\
		RT_UNLOCK(_rt);					\
	}							\
	/* guard against invalid refs */			\
	_rt = 0;						\
} while (0)

#define	RTFREE(_rt) do {					\
	RT_LOCK(_rt);						\
	RTFREE_LOCKED(_rt);					\
} while (0)

#define	RO_RTFREE(_ro) do {					\
	if ((_ro)->ro_rt) {					\
		if ((_ro)->ro_flags & RT_NORTREF) {		\
			(_ro)->ro_flags &= ~RT_NORTREF;		\
			(_ro)->ro_rt = NULL;			\
		} else {					\
			RT_LOCK((_ro)->ro_rt);			\
			RTFREE_LOCKED((_ro)->ro_rt);		\
		}						\
	}							\
} while (0)
#endif

struct radix_node_head *rt_tables_get_rnh(int, int);

struct ifmultiaddr;

void	 rt_ieee80211msg(struct ifnet *, int, void *, size_t);
void	 rt_ifannouncemsg(struct ifnet *, int);
void	 rt_ifmsg(struct ifnet *);
void	 rt_missmsg(int, struct rt_addrinfo *, int, int);
void	 rt_missmsg_fib(int, struct rt_addrinfo *, int, int, int);
void	 rt_newaddrmsg(int, struct bsd_ifaddr *, int, struct rtentry *);
void	 rt_newaddrmsg_fib(int, struct bsd_ifaddr *, int, struct rtentry *, int);
void	 rt_newmaddrmsg(int, struct ifmultiaddr *);
int	 rt_setgate(struct rtentry *, struct bsd_sockaddr *, struct bsd_sockaddr *);
void 	 rt_maskedcopy(struct bsd_sockaddr *, struct bsd_sockaddr *, struct bsd_sockaddr *);

/*
 * Note the following locking behavior:
 *
 *    rtalloc_ign() and rtalloc() return ro->ro_rt unlocked
 *
 *    rtalloc1() returns a locked rtentry
 *
 *    rtfree() and RTFREE_LOCKED() require a locked rtentry
 *
 *    RTFREE() uses an unlocked entry.
 */

int	 rtexpunge(struct rtentry *);
void	 rtfree(struct rtentry *);
int	 rt_check(struct rtentry **, struct rtentry **, struct bsd_sockaddr *);

/* XXX MRT COMPAT VERSIONS THAT SET UNIVERSE to 0 */
/* Thes are used by old code not yet converted to use multiple FIBS */
int	 rt_getifa(struct rt_addrinfo *);
void	 rtalloc_ign(struct route *ro, u_long ignflags);
void	 rtalloc(struct route *ro); /* XXX deprecated, use rtalloc_ign(ro, 0) */
struct rtentry *rtalloc1(struct bsd_sockaddr *, int, u_long);
int	 rtinit(struct bsd_ifaddr *, int, int);
int	 rtioctl(u_long, caddr_t);
void	 rtredirect(struct bsd_sockaddr *, struct bsd_sockaddr *,
	    struct bsd_sockaddr *, int, struct bsd_sockaddr *);
int	 rtrequest(int, struct bsd_sockaddr *,
	    struct bsd_sockaddr *, struct bsd_sockaddr *, int, struct rtentry **);

#ifndef BURN_BRIDGES
/* defaults to "all" FIBs */
int	 rtinit_fib(struct bsd_ifaddr *, int, int);
#endif

__BEGIN_DECLS

/* XXX MRT NEW VERSIONS THAT USE FIBs
 * For now the protocol indepedent versions are the same as the AF_INET ones
 * but this will change.. 
 */
int	 rt_getifa_fib(struct rt_addrinfo *, u_int fibnum);
void	 rtalloc_ign_fib(struct route *ro, u_long ignflags, u_int fibnum);
void	 rtalloc_fib(struct route *ro, u_int fibnum);
struct rtentry *rtalloc1_fib(struct bsd_sockaddr *, int, u_long, u_int);
int	 rtioctl_fib(u_long, caddr_t, u_int);
void	 rtredirect_fib(struct bsd_sockaddr *, struct bsd_sockaddr *,
	    struct bsd_sockaddr *, int, struct bsd_sockaddr *, u_int);
int	 rtrequest_fib(int, struct bsd_sockaddr *,
	    struct bsd_sockaddr *, struct bsd_sockaddr *, int, struct rtentry **, u_int);
int	 rtrequest1_fib(int, struct rt_addrinfo *, struct rtentry **, u_int);

__END_DECLS

#include <bsd/sys/sys/eventhandler.h>
typedef void (*rtevent_arp_update_fn)(void *, struct rtentry *, uint8_t *, struct bsd_sockaddr *);
typedef void (*rtevent_redirect_fn)(void *, struct rtentry *, struct rtentry *, struct bsd_sockaddr *);
/* route_arp_update_event is no longer generated; see arp_update_event */
EVENTHANDLER_DECLARE(route_arp_update_event, rtevent_arp_update_fn);
EVENTHANDLER_DECLARE(route_redirect_event, rtevent_redirect_fn);
#endif

#endif
