/*-
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	From: @(#)if.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef	_NET_IF_VAR_H_
#define	_NET_IF_VAR_H_

#include <osv/net_channel.hh>

/*
 * Structures defining a network interface, providing a packet
 * transport mechanism (ala level 0 of the PUP protocols).
 *
 * Each interface accepts output datagrams of a specified maximum
 * length, and provides higher level routines with input datagrams
 * received from its medium.
 *
 * Output occurs when the routine if_output is called, with three parameters:
 *	(*ifp->if_output)(ifp, m, dst, rt)
 * Here m is the mbuf chain to be sent and dst is the destination address.
 * The output routine encapsulates the supplied datagram if necessary,
 * and then transmits it on its medium.
 *
 * On input, each interface unwraps the data received by it, and either
 * places it on the input queue of an internetwork datagram routine
 * and posts the associated software interrupt, or passes the datagram to a raw
 * packet input routine.
 *
 * Routines exist for locating interfaces by their addresses
 * or for locating an interface on a certain network, as well as more general
 * routing and gateway routines maintaining information used to locate
 * interfaces.  These routines live in the files if.c and route.c
 */

#ifdef __STDC__
/*
 * Forward structure declarations for function prototypes [sic].
 */
struct	mbuf;
struct	thread;
struct	rtentry;
struct	rt_addrinfo;
struct	socket;
struct	ether_header;
struct	carp_if;
struct  ifvlantrunk;
struct	route;
struct	vnet;
#endif

#include <bsd/sys/sys/queue.h>		/* get TAILQ macros */

#ifdef _KERNEL
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/eventhandler.h>
#include <bsd/sys/net/vnet.h>
#endif /* _KERNEL */

#include <bsd/sys/net/if.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/porting/rwlock.h>
#include <bsd/porting/sync_stub.h>
#include <osv/net_channel.hh>


__BEGIN_DECLS

#define	IF_DUNIT_NONE	-1

TAILQ_HEAD(ifnethead, ifnet);	/* we use TAILQs so that the order of */
TAILQ_HEAD(ifaddrhead, bsd_ifaddr);	/* instantiation is preserved in the list */
TAILQ_HEAD(ifprefixhead, ifprefix);
TAILQ_HEAD(ifmultihead, ifmultiaddr);
TAILQ_HEAD(ifgrouphead, ifg_group);

/*
 * Structure defining a queue for a network interface.
 */
struct	ifqueue {
	struct	mbuf *ifq_head;
	struct	mbuf *ifq_tail;
	int	ifq_len;
	int	ifq_maxlen;
	int	ifq_drops;
	struct	mtx ifq_mtx;
};

/*
 * Structure defining a network interface.
 *
 * (Would like to call this struct ``if'', but C isn't PL/1.)
 */

struct ifnet {
	void	*if_softc;		/* pointer to driver state */
	void	*if_l2com;		/* pointer to protocol bits */
	struct vnet *if_vnet;		/* pointer to network stack instance */
	TAILQ_ENTRY(ifnet) if_link; 	/* all struct ifnets are chained */
	char	if_xname[IFNAMSIZ];	/* external name (name + unit) */
	const char *if_dname;		/* driver name */
	int	if_dunit;		/* unit or IF_DUNIT_NONE */
	u_int	if_refcount;		/* reference count */
	struct	ifaddrhead if_addrhead;	/* linked list of addresses per if */
		/*
		 * if_addrhead is the list of all addresses associated to
		 * an interface.
		 * Some code in the kernel assumes that first element
		 * of the list has type AF_LINK, and contains bsd_sockaddr_dl
		 * addresses which store the link-level address and the name
		 * of the interface.
		 * However, access to the AF_LINK address through this
		 * field is deprecated. Use if_addr or ifaddr_byindex() instead.
		 */
	int	if_pcount;		/* number of promiscuous listeners */
	struct	carp_if *if_carp;	/* carp interface structure */
	struct	bpf_if *if_bpf;		/* packet filter structure */
	u_short	if_index;		/* numeric abbreviation for this if  */
	short	if_index_reserved;	/* spare space to grow if_index */
	struct  ifvlantrunk *if_vlantrunk; /* pointer to 802.1q data */
	int	if_flags;		/* up/down, broadcast, etc. */
	int	if_capabilities;	/* interface features & capabilities */
	int	if_capenable;		/* enabled features & capabilities */
	void	*if_linkmib;		/* link-type-specific MIB data */
	size_t	if_linkmiblen;		/* length of above data */
	struct	if_data if_data;
	struct	ifmultihead if_multiaddrs; /* multicast addresses configured */
	int	if_amcount;		/* number of all-multicast requests */
/* procedure handles */
	int	(*if_output)		/* output routine (enqueue) */
		(struct ifnet *, struct mbuf *, struct bsd_sockaddr *,
		     struct route *);
	void	(*if_input)		/* input routine (from h/w driver) */
		(struct ifnet *, struct mbuf *);
	void	(*if_start)		/* initiate output routine */
		(struct ifnet *);
	int	(*if_ioctl)		/* ioctl routine */
		(struct ifnet *, u_long, caddr_t);
	void	(*if_init)		/* Init routine */
		(void *);
	int	(*if_resolvemulti)	/* validate/resolve multicast */
		(struct ifnet *, struct bsd_sockaddr **, struct bsd_sockaddr *);
	void	(*if_qflush)		/* flush any queues */
		(struct ifnet *);
	int	(*if_transmit)		/* initiate output routine */
		(struct ifnet *, struct mbuf *);
	void	(*if_reassign)		/* reassign to vnet routine */
		(struct ifnet *, struct vnet *, char *);
	/*
	 * get the interface info and statistics including the one gathered by HW
	 */
	void (*if_getinfo)(struct ifnet *, struct if_data *);
	classifier if_classifier;

	struct	vnet *if_home_vnet;	/* where this ifnet originates from */
	struct	bsd_ifaddr	*if_addr;	/* pointer to link-level address */
	void	*if_llsoftc;		/* link layer softc */
	int	if_drv_flags;		/* driver-managed status flags */
	struct  ifqueue if_snd;		/* output queue */
	const u_int8_t *if_broadcastaddr; /* linklevel broadcast bytestring */

	void	*if_bridge;		/* bridge glue */

	struct	label *if_label;	/* interface MAC label */

	/* these are only used by IPv6 */
	struct	ifprefixhead if_prefixhead; /* list of prefixes per if */
	void	*if_afdata[AF_MAX];
	int	if_afdata_initialized;
	struct	rwlock if_afdata_lock;
	struct	mtx if_addr_mtx;	/* mutex to protect address lists */

	LIST_ENTRY(ifnet) if_clones;	/* interfaces of a cloner */
	TAILQ_HEAD(, ifg_list) if_groups; /* linked list of groups per if */
					/* protected by if_addr_mtx */
	void	*if_pf_kif;
	void	*if_lagg;		/* lagg glue */
	char	*if_description;	/* interface description */
	u_int	if_fib;			/* interface FIB */
	u_char	if_alloctype;		/* if_type at time of allocation */

	/*
	 * Spare fields are added so that we can modify sensitive data
	 * structures without changing the kernel binary interface, and must
	 * be used with care where binary compatibility is required.
	 */
	char	if_cspare[3];
	int	if_ispare[4];
	void	*if_pspare[8];		/* 1 netmap, 7 TDB */

	void add_net_channel(net_channel* nc, ipv4_tcp_conn_id id) { if_classifier.add(id, nc); }
	void del_net_channel(ipv4_tcp_conn_id id) { if_classifier.remove(id); }
};

typedef void if_init_f_t(void *);

/*
 * XXX These aliases are terribly dangerous because they could apply
 * to anything.
 */
#define	if_mtu		if_data.ifi_mtu
#define	if_type		if_data.ifi_type
#define if_physical	if_data.ifi_physical
#define	if_addrlen	if_data.ifi_addrlen
#define	if_hdrlen	if_data.ifi_hdrlen
#define	if_metric	if_data.ifi_metric
#define	if_link_state	if_data.ifi_link_state
#define	if_baudrate	if_data.ifi_baudrate
#define	if_hwassist	if_data.ifi_hwassist
#define	if_ipackets	if_data.ifi_ipackets
#define	if_ierrors	if_data.ifi_ierrors
#define	if_opackets	if_data.ifi_opackets
#define	if_oerrors	if_data.ifi_oerrors
#define	if_collisions	if_data.ifi_collisions
#define	if_ibytes	if_data.ifi_ibytes
#define	if_obytes	if_data.ifi_obytes
#define	if_imcasts	if_data.ifi_imcasts
#define	if_omcasts	if_data.ifi_omcasts
#define	if_iqdrops	if_data.ifi_iqdrops
#define	if_noproto	if_data.ifi_noproto
#define	if_lastchange	if_data.ifi_lastchange

/* for compatibility with other BSDs */
#define	if_addrlist	if_addrhead
#define	if_list		if_link
#define	if_name(ifp)	((ifp)->if_xname)

/*
 * Locks for address lists on the network interface.
 */
#define	IF_ADDR_WLOCK(if)	mtx_lock(&(if)->if_addr_mtx)
#define	IF_ADDR_WUNLOCK(if)	mtx_unlock(&(if)->if_addr_mtx)
#define	IF_ADDR_RLOCK(if)	mtx_lock(&(if)->if_addr_mtx)
#define	IF_ADDR_RUNLOCK(if)	mtx_unlock(&(if)->if_addr_mtx)
#define	IF_ADDR_LOCK_ASSERT(if)	mtx_assert(&(if)->if_addr_mtx, MA_OWNED)
#define	IF_ADDR_WLOCK_ASSERT(if)	mtx_assert(&(if)->if_addr_mtx, MA_OWNED)
/* XXX: Compat. */
#define	IF_ADDR_LOCK(if)	IF_ADDR_WLOCK(if)
#define	IF_ADDR_UNLOCK(if)	IF_ADDR_WUNLOCK(if)

/*
 * Function variations on locking macros intended to be used by loadable
 * kernel modules in order to divorce them from the internals of address list
 * locking.
 */
void	if_addr_rlock(struct ifnet *ifp);	/* if_addrhead */
void	if_addr_runlock(struct ifnet *ifp);	/* if_addrhead */
void	if_maddr_rlock(struct ifnet *ifp);	/* if_multiaddrs */
void	if_maddr_runlock(struct ifnet *ifp);	/* if_multiaddrs */

/*
 * Output queues (ifp->if_snd) and slow device input queues (*ifp->if_slowq)
 * are queues of messages stored on ifqueue structures
 * (defined above).  Entries are added to and deleted from these structures
 * by these macros, which should be called with ipl raised to splimp().
 */
#define IF_LOCK(ifq)		mtx_lock(&(ifq)->ifq_mtx)
#define IF_UNLOCK(ifq)		mtx_unlock(&(ifq)->ifq_mtx)
#define	IF_LOCK_ASSERT(ifq)	mtx_assert(&(ifq)->ifq_mtx, MA_OWNED)
#define	_IF_QFULL(ifq)		((ifq)->ifq_len >= (ifq)->ifq_maxlen)
#define	_IF_DROP(ifq)		((ifq)->ifq_drops++)
#define	_IF_QLEN(ifq)		((ifq)->ifq_len)

#define	_IF_ENQUEUE(ifq, m) do { 				\
	(m)->m_hdr.mh_nextpkt = NULL;					\
	if ((ifq)->ifq_tail == NULL) 				\
		(ifq)->ifq_head = m; 				\
	else 							\
		(ifq)->ifq_tail->m_hdr.mh_nextpkt = m; 		\
	(ifq)->ifq_tail = m; 					\
	(ifq)->ifq_len++; 					\
} while (0)

#define IF_ENQUEUE(ifq, m) do {					\
	IF_LOCK(ifq); 						\
	_IF_ENQUEUE(ifq, m); 					\
	IF_UNLOCK(ifq); 					\
} while (0)

#define	_IF_PREPEND(ifq, m) do {				\
	(m)->m_hdr.mh_nextpkt = (ifq)->ifq_head; 			\
	if ((ifq)->ifq_tail == NULL) 				\
		(ifq)->ifq_tail = (m); 				\
	(ifq)->ifq_head = (m); 					\
	(ifq)->ifq_len++; 					\
} while (0)

#define IF_PREPEND(ifq, m) do {		 			\
	IF_LOCK(ifq); 						\
	_IF_PREPEND(ifq, m); 					\
	IF_UNLOCK(ifq); 					\
} while (0)

#define	_IF_DEQUEUE(ifq, m) do { 				\
	(m) = (ifq)->ifq_head; 					\
	if (m) { 						\
		if (((ifq)->ifq_head = (m)->m_hdr.mh_nextpkt) == NULL)	\
			(ifq)->ifq_tail = NULL; 		\
		(m)->m_hdr.mh_nextpkt = NULL; 				\
		(ifq)->ifq_len--; 				\
	} 							\
} while (0)

#define IF_DEQUEUE(ifq, m) do { 				\
	IF_LOCK(ifq); 						\
	_IF_DEQUEUE(ifq, m); 					\
	IF_UNLOCK(ifq); 					\
} while (0)

#define	_IF_DEQUEUE_ALL(ifq, m) do {				\
	(m) = (ifq)->ifq_head;					\
	(ifq)->ifq_head = (ifq)->ifq_tail = NULL;		\
	(ifq)->ifq_len = 0;					\
} while (0)

#define	IF_DEQUEUE_ALL(ifq, m) do {				\
	IF_LOCK(ifq); 						\
	_IF_DEQUEUE_ALL(ifq, m);				\
	IF_UNLOCK(ifq); 					\
} while (0)

#define	_IF_POLL(ifq, m)	((m) = (ifq)->ifq_head)
#define	IF_POLL(ifq, m)		_IF_POLL(ifq, m)

#define _IF_DRAIN(ifq) do { 					\
	struct mbuf *m; 					\
	for (;;) { 						\
		_IF_DEQUEUE(ifq, m); 				\
		if (m == NULL) 					\
			break; 					\
		m_freem(m); 					\
	} 							\
} while (0)

#define IF_DRAIN(ifq) do {					\
	IF_LOCK(ifq);						\
	_IF_DRAIN(ifq);						\
	IF_UNLOCK(ifq);						\
} while(0)

#ifdef _KERNEL
/* interface link layer address change event */
typedef void (*iflladdr_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(iflladdr_event, iflladdr_event_handler_t);
/* interface address change event */
typedef void (*ifaddr_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(ifaddr_event, ifaddr_event_handler_t);
/* new interface arrival event */
typedef void (*ifnet_arrival_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(ifnet_arrival_event, ifnet_arrival_event_handler_t);
/* interface departure event */
typedef void (*ifnet_departure_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(ifnet_departure_event, ifnet_departure_event_handler_t);
/* Interface link state change event */
typedef void (*ifnet_link_event_handler_t)(void *, struct ifnet *, int);
EVENTHANDLER_DECLARE(ifnet_link_event, ifnet_link_event_handler_t);

/*
 * interface groups
 */
struct ifg_group {
	char				 ifg_group[IFNAMSIZ];
	u_int				 ifg_refcnt;
	void				*ifg_pf_kif;
	TAILQ_HEAD(, ifg_member)	 ifg_members;
	TAILQ_ENTRY(ifg_group)		 ifg_next;
};

struct ifg_member {
	TAILQ_ENTRY(ifg_member)	 ifgm_next;
	struct ifnet		*ifgm_ifp;
};

struct ifg_list {
	struct ifg_group	*ifgl_group;
	TAILQ_ENTRY(ifg_list)	 ifgl_next;
};

/* group attach event */
typedef void (*group_attach_event_handler_t)(void *, struct ifg_group *);
EVENTHANDLER_DECLARE(group_attach_event, group_attach_event_handler_t);
/* group detach event */
typedef void (*group_detach_event_handler_t)(void *, struct ifg_group *);
EVENTHANDLER_DECLARE(group_detach_event, group_detach_event_handler_t);
/* group change event */
typedef void (*group_change_event_handler_t)(void *, const char *);
EVENTHANDLER_DECLARE(group_change_event, group_change_event_handler_t);

#if 0
#define	IF_AFDATA_WLOCK(ifp)	rw_wlock(&(ifp)->if_afdata_lock)
#define	IF_AFDATA_RLOCK(ifp)	rw_rlock(&(ifp)->if_afdata_lock)
#define	IF_AFDATA_WUNLOCK(ifp)	rw_wunlock(&(ifp)->if_afdata_lock)
#define	IF_AFDATA_RUNLOCK(ifp)	rw_runlock(&(ifp)->if_afdata_lock)
#else
#define	IF_AFDATA_WLOCK(ifp)	do { } while (0)
#define	IF_AFDATA_RLOCK(ifp)	do { } while (0)
#define	IF_AFDATA_WUNLOCK(ifp)	do { } while (0)
#define	IF_AFDATA_RUNLOCK(ifp)	do { } while (0)
#endif

#define	IF_AFDATA_LOCK(ifp)	IF_AFDATA_WLOCK(ifp)
#define	IF_AFDATA_UNLOCK(ifp)	IF_AFDATA_WUNLOCK(ifp)
#define	IF_AFDATA_TRYLOCK(ifp)	rw_try_wlock(&(ifp)->if_afdata_lock)

#define	IF_AFDATA_LOCK_ASSERT(ifp)	rw_assert(&(ifp)->if_afdata_lock, RA_LOCKED)
#define	IF_AFDATA_RLOCK_ASSERT(ifp)	rw_assert(&(ifp)->if_afdata_lock, RA_RLOCKED)
#define	IF_AFDATA_WLOCK_ASSERT(ifp)	rw_assert(&(ifp)->if_afdata_lock, RA_WLOCKED)
#define	IF_AFDATA_UNLOCK_ASSERT(ifp)	rw_assert(&(ifp)->if_afdata_lock, RA_UNLOCKED)

int	if_handoff(struct ifqueue *ifq, struct mbuf *m, struct ifnet *ifp,
	    int adjust);
#define	IF_HANDOFF(ifq, m, ifp)			\
	if_handoff((struct ifqueue *)ifq, m, ifp, 0)
#define	IF_HANDOFF_ADJ(ifq, m, ifp, adj)	\
	if_handoff((struct ifqueue *)ifq, m, ifp, adj)

void	if_start(struct ifnet *);

#define	IFQ_ENQUEUE(ifq, m, err)					\
do {									\
	IF_LOCK(ifq);							\
		if (_IF_QFULL(ifq)) {					\
			m_freem(m);					\
			(err) = ENOBUFS;				\
		} else {						\
			_IF_ENQUEUE(ifq, m);				\
			(err) = 0;					\
		}							\
	if (err)							\
		(ifq)->ifq_drops++;					\
	IF_UNLOCK(ifq);							\
} while (0)

#define	IFQ_DEQUEUE_NOLOCK(ifq, m)					\
do {									\
	if (TBR_IS_ENABLED(ifq))					\
		(m) = tbr_dequeue_ptr(ifq, ALTDQ_REMOVE);		\
	else								\
		_IF_DEQUEUE(ifq, m);					\
} while (0)

#define	IFQ_DEQUEUE(ifq, m)						\
do {									\
	IF_LOCK(ifq);							\
	IFQ_DEQUEUE_NOLOCK(ifq, m);					\
	IF_UNLOCK(ifq);							\
} while (0)

#define	IFQ_POLL_NOLOCK(ifq, m)						\
do {									\
	if (TBR_IS_ENABLED(ifq))					\
		(m) = tbr_dequeue_ptr(ifq, ALTDQ_POLL);			\
	else if (ALTQ_IS_ENABLED(ifq))					\
		ALTQ_POLL(ifq, m);					\
	else								\
		_IF_POLL(ifq, m);					\
} while (0)

#define	IFQ_POLL(ifq, m)						\
do {									\
	IF_LOCK(ifq);							\
	IFQ_POLL_NOLOCK(ifq, m);					\
	IF_UNLOCK(ifq);							\
} while (0)

#define	IFQ_PURGE_NOLOCK(ifq)						\
do {									\
	if (ALTQ_IS_ENABLED(ifq)) {					\
		ALTQ_PURGE(ifq);					\
	} else								\
		_IF_DRAIN(ifq);						\
} while (0)

#define	IFQ_PURGE(ifq)							\
do {									\
	IF_LOCK(ifq);							\
	IFQ_PURGE_NOLOCK(ifq);						\
	IF_UNLOCK(ifq);							\
} while (0)

#define	IFQ_SET_READY(ifq)						\
	do { ((ifq)->altq_flags |= ALTQF_READY); } while (0)

#define	IFQ_LOCK(ifq)			IF_LOCK(ifq)
#define	IFQ_UNLOCK(ifq)			IF_UNLOCK(ifq)
#define	IFQ_LOCK_ASSERT(ifq)		IF_LOCK_ASSERT(ifq)
#define	IFQ_IS_EMPTY(ifq)		((ifq)->ifq_len == 0)
#define	IFQ_INC_LEN(ifq)		((ifq)->ifq_len++)
#define	IFQ_DEC_LEN(ifq)		(--(ifq)->ifq_len)
#define	IFQ_INC_DROPS(ifq)		((ifq)->ifq_drops++)
#define	IFQ_SET_MAXLEN(ifq, len)	((ifq)->ifq_maxlen = (len))

/*
 * The IFF_DRV_OACTIVE test should really occur in the device driver, not in
 * the handoff logic, as that flag is locked by the device driver.
 */
#define	IFQ_HANDOFF_ADJ(ifp, m, adj, err)				\
do {									\
	int len;							\
	short mflags;							\
									\
	len = (m)->M_dat.MH.MH_pkthdr.len;					\
	mflags = (m)->m_hdr.mh_flags;						\
	IFQ_ENQUEUE(&(ifp)->if_snd, m, err);				\
	if ((err) == 0) {						\
		(ifp)->if_obytes += len + (adj);			\
		if (mflags & M_MCAST)					\
			(ifp)->if_omcasts++;				\
		if (((ifp)->if_drv_flags & IFF_DRV_OACTIVE) == 0)	\
			if_start(ifp);					\
	}								\
} while (0)

#define	IFQ_HANDOFF(ifp, m, err)					\
	IFQ_HANDOFF_ADJ(ifp, m, 0, err)

#define	IFQ_DRV_DEQUEUE(ifq, m)						\
do {									\
	(m) = (ifq)->ifq_drv_head;					\
	if (m) {							\
		if (((ifq)->ifq_drv_head = (m)->m_hdr.mh_nextpkt) == NULL)	\
			(ifq)->ifq_drv_tail = NULL;			\
		(m)->m_hdr.mh_nextpkt = NULL;					\
		(ifq)->ifq_drv_len--;					\
	} else {							\
		IFQ_LOCK(ifq);						\
		IFQ_DEQUEUE_NOLOCK(ifq, m);				\
		while ((ifq)->ifq_drv_len < (ifq)->ifq_drv_maxlen) {	\
			struct mbuf *m0;				\
			IFQ_DEQUEUE_NOLOCK(ifq, m0);			\
			if (m0 == NULL)					\
				break;					\
			m0->m_hdr.mh_nextpkt = NULL;				\
			if ((ifq)->ifq_drv_tail == NULL)		\
				(ifq)->ifq_drv_head = m0;		\
			else						\
				(ifq)->ifq_drv_tail->m_hdr.mh_nextpkt = m0;	\
			(ifq)->ifq_drv_tail = m0;			\
			(ifq)->ifq_drv_len++;				\
		}							\
		IFQ_UNLOCK(ifq);					\
	}								\
} while (0)

#define	IFQ_DRV_PREPEND(ifq, m)						\
do {									\
	(m)->m_hdr.mh_nextpkt = (ifq)->ifq_drv_head;				\
	if ((ifq)->ifq_drv_tail == NULL)				\
		(ifq)->ifq_drv_tail = (m);				\
	(ifq)->ifq_drv_head = (m);					\
	(ifq)->ifq_drv_len++;						\
} while (0)

// XXX: dv_len does not exist in ifqueue in BSD as well. This is probably
// used in another structure that supercedes ifq
#define	IFQ_DRV_IS_EMPTY(ifq)						\
	(((ifq)->ifq_len == 0))

#define	IFQ_DRV_PURGE(ifq)						\
do {									\
	struct mbuf *m, *n = (ifq)->ifq_drv_head;			\
	while((m = n) != NULL) {					\
		n = m->m_hdr.mh_nextpkt;					\
		m_freem(m);						\
	}								\
	(ifq)->ifq_drv_head = (ifq)->ifq_drv_tail = NULL;		\
	(ifq)->ifq_drv_len = 0;						\
	IFQ_PURGE(ifq);							\
} while (0)

/*
 * 72 was chosen below because it is the size of a TCP/IP
 * header (40) + the minimum mss (32).
 */
#define	IF_MINMTU	72
#define	IF_MAXMTU	65535

#endif /* _KERNEL */

/*
 * The bsd_ifaddr structure contains information about one address
 * of an interface.  They are maintained by the different address families,
 * are allocated and attached when an address is set, and are linked
 * together so all addresses for an interface can be located.
 *
 * NOTE: a 'struct bsd_ifaddr' is always at the beginning of a larger
 * chunk of malloc'ed memory, where we store the three addresses
 * (ifa_addr, ifa_dstaddr and ifa_netmask) referenced here.
 */
struct bsd_ifaddr {
	struct	bsd_sockaddr *ifa_addr;	/* address of interface */
	struct	bsd_sockaddr *ifa_dstaddr;	/* other end of p-to-p link */
#define	ifa_broadaddr	ifa_dstaddr	/* broadcast address interface */
	struct	bsd_sockaddr *ifa_netmask;	/* used to determine subnet */
	struct	if_data if_data;	/* not all members are meaningful */
	struct	ifnet *ifa_ifp;		/* back-pointer to interface */
	TAILQ_ENTRY(bsd_ifaddr) ifa_link;	/* queue macro glue */
	void	(*ifa_rtrequest)	/* check or clean routes (+ or -)'d */
		(int, struct rtentry *, struct rt_addrinfo *);
	u_short	ifa_flags;		/* mostly rt_flags for cloning */
	u_int	ifa_refcnt;		/* references to this structure */
	int	ifa_metric;		/* cost of going out this interface */
	int (*ifa_claim_addr)		/* check if an addr goes to this if */
		(struct bsd_ifaddr *, struct bsd_sockaddr *);
	struct mtx ifa_mtx;
};
#define	IFA_ROUTE	RTF_UP		/* route installed */
#define IFA_RTSELF	RTF_HOST	/* loopback route to self installed */

/* for compatibility with other BSDs */
#define	ifa_list	ifa_link

#ifdef _KERNEL
#define	IFA_LOCK(ifa)		mtx_lock(&(ifa)->ifa_mtx)
#define	IFA_UNLOCK(ifa)		mtx_unlock(&(ifa)->ifa_mtx)

void	ifa_free(struct bsd_ifaddr *ifa);
void	ifa_init(struct bsd_ifaddr *ifa);
void	ifa_ref(struct bsd_ifaddr *ifa);
#endif

/*
 * The prefix structure contains information about one prefix
 * of an interface.  They are maintained by the different address families,
 * are allocated and attached when a prefix or an address is set,
 * and are linked together so all prefixes for an interface can be located.
 */
struct ifprefix {
	struct	bsd_sockaddr *ifpr_prefix;	/* prefix of interface */
	struct	ifnet *ifpr_ifp;	/* back-pointer to interface */
	TAILQ_ENTRY(ifprefix) ifpr_list; /* queue macro glue */
	u_char	ifpr_plen;		/* prefix length in bits */
	u_char	ifpr_type;		/* protocol dependent prefix type */
};

/*
 * Multicast address structure.  This is analogous to the bsd_ifaddr
 * structure except that it keeps track of multicast addresses.
 */
struct ifmultiaddr {
	TAILQ_ENTRY(ifmultiaddr) ifma_link; /* queue macro glue */
	struct	bsd_sockaddr *ifma_addr; 	/* address this membership is for */
	struct	bsd_sockaddr *ifma_lladdr;	/* link-layer translation, if any */
	struct	ifnet *ifma_ifp;	/* back-pointer to interface */
	u_int	ifma_refcount;		/* reference count */
	void	*ifma_protospec;	/* protocol-specific state, if any */
	struct	ifmultiaddr *ifma_llifma; /* pointer to ifma for ifma_lladdr */
};

#ifdef _KERNEL

extern	struct rwlock ifnet_rwlock;

#define IFNET_LOCK_INIT()   rw_init_flags(&ifnet_rwlock, "ifnet_rw",  RW_RECURSE)
#define IFNET_WLOCK()       rw_wlock(&ifnet_rwlock)
#define IFNET_WUNLOCK()     rw_wunlock(&ifnet_rwlock)

/*
 * To assert the ifnet lock, you must know not only whether it's for read or
 * write, but also whether it was acquired with sleep support or not.
 */
#define	IFNET_RLOCK_ASSERT()		    rw_assert(&ifnet_rwlock, RA_RLOCKED)
#define	IFNET_RLOCK_NOSLEEP_ASSERT()	rw_assert(&ifnet_rwlock, RA_RLOCKED)
#define	IFNET_WLOCK_ASSERT()            rw_assert(&ifnet_rwlock, RA_WLOCKED)

#define	IFNET_RLOCK()		            rw_rlock(&ifnet_rwlock)
#define	IFNET_RLOCK_NOSLEEP()	        rw_rlock(&ifnet_rwlock)
#define	IFNET_RUNLOCK()		            rw_runlock(&ifnet_rwlock)
#define	IFNET_RUNLOCK_NOSLEEP()	        rw_runlock(&ifnet_rwlock)

/*
 * Look up an ifnet given its index; the _ref variant also acquires a
 * reference that must be freed using if_rele().  It is almost always a bug
 * to call ifnet_byindex() instead if ifnet_byindex_ref().
 */
struct ifnet	*ifnet_byindex(u_short idx);
struct ifnet	*ifnet_byindex_locked(u_short idx);
struct ifnet	*ifnet_byindex_ref(u_short idx);

/*
 * Given the index, ifaddr_byindex() returns the one and only
 * link-level bsd_ifaddr for the interface. You are not supposed to use
 * it to traverse the list of addresses associated to the interface.
 */
struct bsd_ifaddr	*ifaddr_byindex(u_short idx);

VNET_DECLARE(struct ifnethead, ifnet);
VNET_DECLARE(struct ifgrouphead, ifg_head);
VNET_DECLARE(int, if_index);
VNET_DECLARE(struct ifnet *, loif);	/* first loopback interface */
VNET_DECLARE(int, useloopback);

#define	V_ifnet		VNET(ifnet)
#define	V_ifg_head	VNET(ifg_head)
#define	V_if_index	VNET(if_index)
#define	V_loif		VNET(loif)
#define	V_useloopback	VNET(useloopback)

extern	int ifqmaxlen;

int	if_addgroup(struct ifnet *, const char *);
int	if_delgroup(struct ifnet *, const char *);
int	if_addmulti(struct ifnet *, struct bsd_sockaddr *, struct ifmultiaddr **);
int	if_allmulti(struct ifnet *, int);
struct	ifnet* if_alloc(u_char);
void	if_attach(struct ifnet *);
void	if_dead(struct ifnet *);
int	if_delmulti(struct ifnet *, struct bsd_sockaddr *);
void	if_delmulti_ifma(struct ifmultiaddr *);
void	if_detach(struct ifnet *);
void	if_vmove(struct ifnet *, struct vnet *);
void	if_purgeaddrs(struct ifnet *);
void	if_delallmulti(struct ifnet *);
void	if_down(struct ifnet *);
struct ifmultiaddr *
	if_findmulti(struct ifnet *, struct bsd_sockaddr *);
void	if_free(struct ifnet *);
void	if_free_type(struct ifnet *, u_char);
void	if_initname(struct ifnet *, const char *, int);
void	if_link_state_change(struct ifnet *, int);
int	if_printf(struct ifnet *, const char *, ...) __printflike(2, 3);
void	if_qflush(struct ifnet *);
void	if_ref(struct ifnet *);
void	if_rele(struct ifnet *);
int	if_setlladdr(struct ifnet *, const u_char *, int);
void	if_up(struct ifnet *);
int	ifioctl(struct socket *, u_long, caddr_t, struct thread *);
int	ifpromisc(struct ifnet *, int);
struct	ifnet *ifunit(const char *);
struct	ifnet *ifunit_ref(const char *);

void	ifq_init(struct ifqueue *, struct ifnet *ifp);
void	ifq_delete(struct ifqueue *);

int	ifa_add_loopback_route(struct bsd_ifaddr *, struct bsd_sockaddr *);
int	ifa_del_loopback_route(struct bsd_ifaddr *, struct bsd_sockaddr *);

struct	bsd_ifaddr *ifa_ifwithaddr(struct bsd_sockaddr *);
int		ifa_ifwithaddr_check(struct bsd_sockaddr *);
struct	bsd_ifaddr *ifa_ifwithbroadaddr(struct bsd_sockaddr *);
struct	bsd_ifaddr *ifa_ifwithdstaddr(struct bsd_sockaddr *);
struct	bsd_ifaddr *ifa_ifwithnet(struct bsd_sockaddr *, int);
struct	bsd_ifaddr *ifa_ifwithroute(int, struct bsd_sockaddr *, struct bsd_sockaddr *);
struct	bsd_ifaddr *ifa_ifwithroute_fib(int, struct bsd_sockaddr *, struct bsd_sockaddr *, u_int);

struct	bsd_ifaddr *ifaof_ifpforaddr(struct bsd_sockaddr *, struct ifnet *);

int	if_simloop(struct ifnet *ifp, struct mbuf *m, int af, int hlen);

typedef	void *if_com_alloc_t(u_char type, struct ifnet *ifp);
typedef	void if_com_free_t(void *com, u_char type);
void	if_register_com_alloc(u_char type, if_com_alloc_t *a, if_com_free_t *f);
void	if_deregister_com_alloc(u_char type);

#define IF_LLADDR(ifp)							\
    LLADDR((struct bsd_sockaddr_dl *)((ifp)->if_addr->ifa_addr))

#endif /* _KERNEL */

__END_DECLS

#endif /* !_NET_IF_VAR_H_ */
