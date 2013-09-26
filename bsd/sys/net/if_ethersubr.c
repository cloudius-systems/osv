/*-
 * Copyright (c) 1982, 1989, 1993
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
 *	@(#)if_ethersubr.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#include <assert.h>

#include <osv/ioctl.h>

#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>
#include <bsd/porting/rwlock.h>

#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/sbuf.h>
#include <bsd/sys/sys/socket.h>

#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_arp.h>
#include <bsd/sys/net/netisr.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/net/if_llc.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_types.h>
#if 0
#include <net/bpf.h>
#include <bsd/sys/net/if_bridgevar.h>
#include <bsd/sys/net/if_vlan_var.h>
#include <bsd/sys/net/pf_mtag.h>
#endif
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/contrib/pf/net/pf_mtag.h>
#include <bsd/sys/net/vnet.h>

#if defined(INET) || defined(INET6)
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/netinet/if_ether.h>
#if 0
#include <bsd/sys/netinet/ip_var.h>
#include <bsd/sys/netinet/ip_fw.h>
#include <bsd/sys/netpfil/ipfw/ip_fw_private.h>
#endif
#endif
#ifdef INET6
#include <netinet6/nd6.h>
#endif

int (*ef_inputp)(struct ifnet*, struct ether_header *eh, struct mbuf *m);
int (*ef_outputp)(struct ifnet *ifp, struct mbuf **mp,
		struct bsd_sockaddr *dst, short *tp, int *hlen);

#ifdef CTASSERT
CTASSERT(sizeof (struct ether_header) == ETHER_ADDR_LEN * 2 + 2);
CTASSERT(sizeof (struct ether_addr) == ETHER_ADDR_LEN);
#endif

#if 0
/* netgraph node hooks for ng_ether(4) */
void	(*ng_ether_input_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_input_orphan_p)(struct ifnet *ifp, struct mbuf *m);
int	(*ng_ether_output_p)(struct ifnet *ifp, struct mbuf **mp);
void	(*ng_ether_attach_p)(struct ifnet *ifp);
void	(*ng_ether_detach_p)(struct ifnet *ifp);

void	(*vlan_input_p)(struct ifnet *, struct mbuf *);
#endif

#if 0
/* if_bridge(4) support */
struct mbuf *(*bridge_input_p)(struct ifnet *, struct mbuf *); 
int	(*bridge_output_p)(struct ifnet *, struct mbuf *, 
		struct bsd_sockaddr *, struct rtentry *);
void	(*bridge_dn_p)(struct mbuf *, struct ifnet *);
#endif

#if 0
/* if_lagg(4) support */
struct mbuf *(*lagg_input_p)(struct ifnet *, struct mbuf *); 
#endif

static const u_char etherbroadcastaddr[ETHER_ADDR_LEN] =
			{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static	int ether_resolvemulti(struct ifnet *, struct bsd_sockaddr **,
		struct bsd_sockaddr *);

/* XXX: should be in an arp support file, not here */
MALLOC_DEFINE(M_ARPCOM, "arpcom", "802.* interface internals");

#define	ETHER_IS_BROADCAST(addr) \
	(bcmp(etherbroadcastaddr, (addr), ETHER_ADDR_LEN) == 0)

#define senderr(e) do { error = (e); goto bad;} while (0)

#if 0
#if defined(INET) || defined(INET6)
int
ether_ipfw_chk(struct mbuf **m0, struct ifnet *dst, int shared);
static VNET_DEFINE(int, ether_ipfw);
#define	V_ether_ipfw	VNET(ether_ipfw)
#endif
#endif

/*
 * Ethernet output routine.
 * Encapsulate a packet of type family for the local net.
 * Use trailer local net encapsulation if enough data in first
 * packet leaves a multiple of 512 bytes of data in remainder.
 */
int
ether_output(struct ifnet *ifp, struct mbuf *m,
	struct bsd_sockaddr *dst, struct route *ro)
{
	short type;
	int error = 0, hdrcmplt = 0;
	u_char esrc[ETHER_ADDR_LEN], edst[ETHER_ADDR_LEN];
	struct llentry *lle = NULL;
	struct rtentry *rt0 = NULL;
	struct ether_header *eh;
	struct pf_mtag *t;
	int loop_copy = 1;
	int hlen;	/* link layer header length */

	if (ro != NULL) {
		if (!(m->m_flags & (M_BCAST | M_MCAST)))
			lle = ro->ro_lle;
		rt0 = ro->ro_rt;
	}

	M_PROFILE(m);
	if (ifp->if_flags & IFF_MONITOR)
		senderr(ENETDOWN);
	if (!((ifp->if_flags & IFF_UP) &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING)))
		senderr(ENETDOWN);

	hlen = ETHER_HDR_LEN;
	switch (dst->sa_family) {
#ifdef INET
	case AF_INET:
		if (lle != NULL && (lle->la_flags & LLE_VALID))
			memcpy(edst, &lle->ll_addr.mac16, sizeof(edst));
		else
			error = arpresolve(ifp, rt0, m, dst, edst, &lle);
		if (error)
			return (error == EWOULDBLOCK ? 0 : error);
		type = htons(ETHERTYPE_IP);
		break;
	case AF_ARP:
	{
		struct arphdr *ah;
		ah = mtod(m, struct arphdr *);
		ah->ar_hrd = htons(ARPHRD_ETHER);

		loop_copy = 0; /* if this is for us, don't do it */

		switch(ntohs(ah->ar_op)) {
		case ARPOP_REVREQUEST:
		case ARPOP_REVREPLY:
			type = htons(ETHERTYPE_REVARP);
			break;
		case ARPOP_REQUEST:
		case ARPOP_REPLY:
		default:
			type = htons(ETHERTYPE_ARP);
			break;
		}

		if (m->m_flags & M_BCAST)
			bcopy(ifp->if_broadcastaddr, edst, ETHER_ADDR_LEN);
		else
			bcopy(ar_tha(ah), edst, ETHER_ADDR_LEN);

	}
	break;
#endif
#ifdef INET6
	case AF_INET6:
		if (lle != NULL && (lle->la_flags & LLE_VALID))
			memcpy(edst, &lle->ll_addr.mac16, sizeof(edst));
		else
			error = nd6_storelladdr(ifp, m, dst, (u_char *)edst, &lle);
		if (error)
			return error;
		type = htons(ETHERTYPE_IPV6);
		break;
#endif

	case pseudo_AF_HDRCMPLT:
		hdrcmplt = 1;
		eh = (struct ether_header *)dst->sa_data;
		(void)memcpy(esrc, eh->ether_shost, sizeof (esrc));
		/* FALLTHROUGH */

	case AF_UNSPEC:
		loop_copy = 0; /* if this is for us, don't do it */
		eh = (struct ether_header *)dst->sa_data;
		(void)memcpy(edst, eh->ether_dhost, sizeof (edst));
		type = eh->ether_type;
		break;

	default:
		if_printf(ifp, "can't handle af%d\n", dst->sa_family);
		senderr(EAFNOSUPPORT);
	}

	if (lle != NULL && (lle->la_flags & LLE_IFADDR)) {
		int csum_flags = 0;
		if (m->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= (CSUM_IP_CHECKED|CSUM_IP_VALID);
		if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA)
			csum_flags |= (CSUM_DATA_VALID|CSUM_PSEUDO_HDR);
		if (m->m_pkthdr.csum_flags & CSUM_SCTP)
			csum_flags |= CSUM_SCTP_VALID;
		m->m_pkthdr.csum_flags |= csum_flags;
		m->m_pkthdr.csum_data = 0xffff;
		return (if_simloop(ifp, m, dst->sa_family, 0));
	}

	/*
	 * Add local net header.  If no space in first mbuf,
	 * allocate another.
	 */
	M_PREPEND(m, ETHER_HDR_LEN, M_DONTWAIT);
	if (m == NULL)
		senderr(ENOBUFS);
	eh = mtod(m, struct ether_header *);
	(void)memcpy(&eh->ether_type, &type,
		sizeof(eh->ether_type));
	(void)memcpy(eh->ether_dhost, edst, sizeof (edst));
	if (hdrcmplt)
		(void)memcpy(eh->ether_shost, esrc,
			sizeof(eh->ether_shost));
	else
		(void)memcpy(eh->ether_shost, IF_LLADDR(ifp),
			sizeof(eh->ether_shost));

	/*
	 * If a simplex interface, and the packet is being sent to our
	 * Ethernet address or a broadcast address, loopback a copy.
	 * XXX To make a simplex device behave exactly like a duplex
	 * device, we should copy in the case of sending to our own
	 * ethernet address (thus letting the original actually appear
	 * on the wire). However, we don't do that here for security
	 * reasons and compatibility with the original behavior.
	 */
	if ((ifp->if_flags & IFF_SIMPLEX) && loop_copy &&
	    ((t = pf_find_mtag(m)) == NULL || !t->routed)) {
		int csum_flags = 0;

		if (m->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= (CSUM_IP_CHECKED|CSUM_IP_VALID);
		if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA)
			csum_flags |= (CSUM_DATA_VALID|CSUM_PSEUDO_HDR);
		if (m->m_pkthdr.csum_flags & CSUM_SCTP)
			csum_flags |= CSUM_SCTP_VALID;

		if (m->m_flags & M_BCAST) {
			struct mbuf *n;

			/*
			 * Because if_simloop() modifies the packet, we need a
			 * writable copy through m_dup() instead of a readonly
			 * one as m_copy[m] would give us. The alternative would
			 * be to modify if_simloop() to handle the readonly mbuf,
			 * but performancewise it is mostly equivalent (trading
			 * extra data copying vs. extra locking).
			 *
			 * XXX This is a local workaround.  A number of less
			 * often used kernel parts suffer from the same bug.
			 * See PR kern/105943 for a proposed general solution.
			 */
			if ((n = m_dup(m, M_DONTWAIT)) != NULL) {
				n->m_pkthdr.csum_flags |= csum_flags;
				if (csum_flags & CSUM_DATA_VALID)
					n->m_pkthdr.csum_data = 0xffff;
				(void)if_simloop(ifp, n, dst->sa_family, hlen);
			} else
				ifp->if_iqdrops++;
		} else if (bcmp(eh->ether_dhost, eh->ether_shost,
				ETHER_ADDR_LEN) == 0) {
			m->m_pkthdr.csum_flags |= csum_flags;
			if (csum_flags & CSUM_DATA_VALID)
				m->m_pkthdr.csum_data = 0xffff;
			(void) if_simloop(ifp, m, dst->sa_family, hlen);
			return (0);	/* XXX */
		}
	}

	goto good;

#if 0
       /*
	* Bridges require special output handling.
	*/
	if (ifp->if_bridge) {
		BRIDGE_OUTPUT(ifp, m, error);
		return (error);
	}

#if defined(INET) || defined(INET6)
	if (ifp->if_carp &&
	    (error = (*carp_output_p)(ifp, m, dst, NULL)))
		goto bad;
#endif

	/* Handle ng_ether(4) processing, if any */
	if (IFP2AC(ifp)->ac_netgraph != NULL) {
		KASSERT(ng_ether_output_p != NULL,
		    ("ng_ether_output_p is NULL"));
		if ((error = (*ng_ether_output_p)(ifp, &m)) != 0) {
bad:			if (m != NULL)
				m_freem(m);
			return (error);
		}
		if (m == NULL)
			return (0);
	}
#else
	bad:            if (m != NULL)
	                m_freem(m);
	            return (error);
#endif
	good:

	/* Continue with link-layer output */
	return ether_output_frame(ifp, m);
}

/*
 * Ethernet link layer output routine to send a raw frame to the device.
 *
 * This assumes that the 14 byte Ethernet header is present and contiguous
 * in the first mbuf (if BRIDGE'ing).
 */
int
ether_output_frame(struct ifnet *ifp, struct mbuf *m)
{
#if 0
#if defined(INET) || defined(INET6)

	if (V_ip_fw_chk_ptr && V_ether_ipfw != 0) {
		if (ether_ipfw_chk(&m, ifp, 0) == 0) {
			if (m) {
				m_freem(m);
				return EACCES;	/* pkt dropped */
			} else
				return 0;	/* consumed e.g. in a pipe */
		}
	}
#endif
#endif

	/*
	 * Queue message on interface, update output statistics if
	 * successful, and start output if interface not yet active.
	 */
	return ((ifp->if_transmit)(ifp, m));
}

#if 0
#if defined(INET) || defined(INET6)
/*
 * ipfw processing for ethernet packets (in and out).
 * The second parameter is NULL from ether_demux, and ifp from
 * ether_output_frame.
 */
int
ether_ipfw_chk(struct mbuf **m0, struct ifnet *dst, int shared)
{
	struct ether_header *eh;
	struct ether_header save_eh;
	struct mbuf *m;
	int i;
	struct ip_fw_args args;
	struct m_tag *mtag;

	/* fetch start point from rule, if any */
	mtag = m_tag_locate(*m0, MTAG_IPFW_RULE, 0, NULL);
	if (mtag == NULL) {
		args.rule.slot = 0;
	} else {
		/* dummynet packet, already partially processed */
		struct ipfw_rule_ref *r;

		/* XXX can we free it after use ? */
		mtag->m_tag_id = PACKET_TAG_NONE;
		r = (struct ipfw_rule_ref *)(mtag + 1);
		if (r->info & IPFW_ONEPASS)
			return (1);
		args.rule = *r;
	}

	/*
	 * I need some amt of data to be contiguous, and in case others need
	 * the packet (shared==1) also better be in the first mbuf.
	 */
	m = *m0;
	i = min( m->m_pkthdr.len, max_protohdr);
	if ( shared || m->m_len < i) {
		m = m_pullup(m, i);
		if (m == NULL) {
			*m0 = m;
			return 0;
		}
	}
	eh = mtod(m, struct ether_header *);
	save_eh = *eh;			/* save copy for restore below */
	m_adj(m, ETHER_HDR_LEN);	/* strip ethernet header */

	args.m = m;		/* the packet we are looking at		*/
	args.oif = dst;		/* destination, if any			*/
	args.next_hop = NULL;	/* we do not support forward yet	*/
	args.next_hop6 = NULL;	/* we do not support forward yet	*/
	args.eh = &save_eh;	/* MAC header for bridged/MAC packets	*/
	args.inp = NULL;	/* used by ipfw uid/gid/jail rules	*/
	i = V_ip_fw_chk_ptr(&args);
	m = args.m;
	if (m != NULL) {
		/*
		 * Restore Ethernet header, as needed, in case the
		 * mbuf chain was replaced by ipfw.
		 */
		M_PREPEND(m, ETHER_HDR_LEN, M_DONTWAIT);
		if (m == NULL) {
			*m0 = m;
			return 0;
		}
		if (eh != mtod(m, struct ether_header *))
			bcopy(&save_eh, mtod(m, struct ether_header *),
				ETHER_HDR_LEN);
	}
	*m0 = m;

	if (i == IP_FW_DENY) /* drop */
		return 0;

	KASSERT(m != NULL, ("ether_ipfw_chk: m is NULL"));

	if (i == IP_FW_PASS) /* a PASS rule.  */
		return 1;

	if (ip_dn_io_ptr && (i == IP_FW_DUMMYNET)) {
		int dir;
		/*
		 * Pass the pkt to dummynet, which consumes it.
		 * If shared, make a copy and keep the original.
		 */
		if (shared) {
			m = m_copypacket(m, M_DONTWAIT);
			if (m == NULL)
				return 0;
		} else {
			/*
			 * Pass the original to dummynet and
			 * nothing back to the caller
			 */
			*m0 = NULL ;
		}
		dir = PROTO_LAYER2 | (dst ? DIR_OUT : DIR_IN);
		ip_dn_io_ptr(&m, dir, &args);
		return 0;
	}
	/*
	 * XXX at some point add support for divert/forward actions.
	 * If none of the above matches, we have to drop the pkt.
	 */
	return 0;
}
#endif
#endif

/*
 * Process a received Ethernet packet; the packet is in the
 * mbuf chain m with the ethernet header at the front.
 */
static void
ether_input_internal(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_header *eh;
	u_short etype;

	if ((ifp->if_flags & IFF_UP) == 0) {
		m_freem(m);
		return;
	}
#ifdef DIAGNOSTIC
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		if_printf(ifp, "discard frame at !IFF_DRV_RUNNING\n");
		m_freem(m);
		return;
	}
#endif
	/*
	 * Do consistency checks to verify assumptions
	 * made by code past this point.
	 */
	if ((m->m_flags & M_PKTHDR) == 0) {
		if_printf(ifp, "discard frame w/o packet header\n");
		ifp->if_ierrors++;
		m_freem(m);
		return;
	}
	if (m->m_len < ETHER_HDR_LEN) {
		/* XXX maybe should pullup? */
		if_printf(ifp, "discard frame w/o leading ethernet "
				"header (len %u pkt len %u)\n",
				m->m_len, m->m_pkthdr.len);
		ifp->if_ierrors++;
		m_freem(m);
		return;
	}
	eh = mtod(m, struct ether_header *);
	etype = ntohs(eh->ether_type);
	if (m->m_pkthdr.rcvif == NULL) {
		if_printf(ifp, "discard frame w/o interface pointer\n");
		ifp->if_ierrors++;
		m_freem(m);
		return;
	}
#ifdef DIAGNOSTIC
	if (m->m_pkthdr.rcvif != ifp) {
		if_printf(ifp, "Warning, frame marked as received on %s\n",
			m->m_pkthdr.rcvif->if_xname);
	}
#endif

	CURVNET_SET_QUIET(ifp->if_vnet);

	if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
		if (ETHER_IS_BROADCAST(eh->ether_dhost))
			m->m_flags |= M_BCAST;
		else
			m->m_flags |= M_MCAST;
		ifp->if_imcasts++;
	}

#if 0
	/*
	 * Give bpf a chance at the packet.
	 */
	ETHER_BPF_MTAP(ifp, m);
#endif

	/*
	 * If the CRC is still on the packet, trim it off. We do this once
	 * and once only in case we are re-entered. Nothing else on the
	 * Ethernet receive path expects to see the FCS.
	 */
	if (m->m_flags & M_HASFCS) {
		m_adj(m, -ETHER_CRC_LEN);
		m->m_flags &= ~M_HASFCS;
	}

	ifp->if_ibytes += m->m_pkthdr.len;

	/* Allow monitor mode to claim this frame, after stats are updated. */
	if (ifp->if_flags & IFF_MONITOR) {
		m_freem(m);
		CURVNET_RESTORE();
		return;
	}

#if 0
	/* Handle input from a lagg(4) port */
	if (ifp->if_type == IFT_IEEE8023ADLAG) {
		KASSERT(lagg_input_p != NULL,
		    ("%s: if_lagg not loaded!", __func__));
		m = (*lagg_input_p)(ifp, m);
		if (m != NULL)
			ifp = m->m_pkthdr.rcvif;
		else {
			CURVNET_RESTORE();
			return;
		}
	}
#endif

	assert(etype != ETHERTYPE_VLAN);

#if 0
	/*
	 * If the hardware did not process an 802.1Q tag, do this now,
	 * to allow 802.1P priority frames to be passed to the main input
	 * path correctly.
	 * TODO: Deal with Q-in-Q frames, but not arbitrary nesting levels.
	 */
	if ((m->m_flags & M_VLANTAG) == 0 && etype == ETHERTYPE_VLAN) {
		struct ether_vlan_header *evl;

		if (m->m_len < sizeof(*evl) &&
		    (m = m_pullup(m, sizeof(*evl))) == NULL) {
#ifdef DIAGNOSTIC
			if_printf(ifp, "cannot pullup VLAN header\n");
#endif
			ifp->if_ierrors++;
			m_freem(m);
			CURVNET_RESTORE();
			return;
		}

		evl = mtod(m, struct ether_vlan_header *);
		m->m_pkthdr.ether_vtag = ntohs(evl->evl_tag);
		m->m_flags |= M_VLANTAG;

		bcopy((char *)evl, (char *)evl + ETHER_VLAN_ENCAP_LEN,
		    ETHER_HDR_LEN - ETHER_TYPE_LEN);
		m_adj(m, ETHER_VLAN_ENCAP_LEN);
	}
#endif

	M_SETFIB(m, ifp->if_fib);

#if 0
	/* Allow ng_ether(4) to claim this frame. */
	if (IFP2AC(ifp)->ac_netgraph != NULL) {
		KASSERT(ng_ether_input_p != NULL,
		    ("%s: ng_ether_input_p is NULL", __func__));
		m->m_flags &= ~M_PROMISC;
		(*ng_ether_input_p)(ifp, &m);
		if (m == NULL) {
			CURVNET_RESTORE();
			return;
		}
	}
#endif

#if 0
	/*
	 * Allow if_bridge(4) to claim this frame.
	 * The BRIDGE_INPUT() macro will update ifp if the bridge changed it
	 * and the frame should be delivered locally.
	 */
	if (ifp->if_bridge != NULL) {
		m->m_flags &= ~M_PROMISC;
		BRIDGE_INPUT(ifp, m);
		if (m == NULL) {
			CURVNET_RESTORE();
			return;
		}
	}
#endif
	{
		/*
		 * If the frame received was not for our MAC address, set the
		 * M_PROMISC flag on the mbuf chain. The frame may need to
		 * be seen by the rest of the Ethernet input path in case of
		 * re-entry (e.g. bridge, vlan, netgraph) but should not be
		 * seen by upper protocol layers.
		 */
		if (!ETHER_IS_MULTICAST(eh->ether_dhost) &&
		    bcmp(IF_LLADDR(ifp), eh->ether_dhost, ETHER_ADDR_LEN) != 0)
			m->m_flags |= M_PROMISC;
	}

#if 0
	/* First chunk of an mbuf contains good entropy */
	if (harvest.ethernet)
		random_harvest(m, 16, 3, 0, RANDOM_NET);
#endif

	ether_demux(ifp, m);
	CURVNET_RESTORE();
}

/*
 * Ethernet input dispatch; by default, direct dispatch here regardless of
 * global configuration.
 */
static void
ether_nh_input(struct mbuf *m)
{

	ether_input_internal(m->m_pkthdr.rcvif, m);
}

static struct netisr_handler	ether_nh = {
	.nh_name = "ether",
	.nh_handler = ether_nh_input,
	.nh_proto = NETISR_ETHER,
	.nh_policy = NETISR_POLICY_SOURCE,
	.nh_dispatch = NETISR_DISPATCH_DIRECT,
};

void ether_init(void *arg)
{
	netisr_register(&ether_nh);
	/* call if_register_com_alloc(), don't act as a module */
	if_register_com_alloc(IFT_ETHER, ether_alloc, ether_free);
}
SYSINIT(ether, SI_SUB_INIT_IF, SI_ORDER_ANY, ether_init, NULL);

static void
ether_input(struct ifnet *ifp, struct mbuf *m)
{

	/*
	 * We will rely on rcvif being set properly in the deferred context,
	 * so assert it is correct here.
	 */
	KASSERT(m->m_pkthdr.rcvif == ifp, ("%s: ifnet mismatch", __func__));

	netisr_dispatch(NETISR_ETHER, m);
}

/*
 * Upper layer processing for a received Ethernet packet.
 */
void
ether_demux(struct ifnet *ifp, struct mbuf *m)
{
	struct ether_header *eh;
	int isr;
	u_short ether_type;

	KASSERT(ifp != NULL, ("%s: NULL interface pointer", __func__));

#if 0
#if defined(INET) || defined(INET6)
	/*
	 * Allow dummynet and/or ipfw to claim the frame.
	 * Do not do this for PROMISC frames in case we are re-entered.
	 */
	if (V_ip_fw_chk_ptr && V_ether_ipfw != 0 && !(m->m_flags & M_PROMISC)) {
		if (ether_ipfw_chk(&m, NULL, 0) == 0) {
			if (m)
				m_freem(m);	/* dropped; free mbuf chain */
			return;			/* consumed */
		}
	}
#endif
#endif

	eh = mtod(m, struct ether_header *);
	ether_type = ntohs(eh->ether_type);

#if 0
	/*
	 * If this frame has a VLAN tag other than 0, call vlan_input()
	 * if its module is loaded. Otherwise, drop.
	 */
	if ((m->m_flags & M_VLANTAG) &&
	    EVL_VLANOFTAG(m->m_pkthdr.ether_vtag) != 0) {
		if (ifp->if_vlantrunk == NULL) {
			ifp->if_noproto++;
			m_freem(m);
			return;
		}
		KASSERT(vlan_input_p != NULL,("%s: VLAN not loaded!",
		    __func__));
		/* Clear before possibly re-entering ether_input(). */
		m->m_flags &= ~M_PROMISC;
		(*vlan_input_p)(ifp, m);
		return;
	}
#endif

	/*
	 * Pass promiscuously received frames to the upper layer if the user
	 * requested this by setting IFF_PPROMISC. Otherwise, drop them.
	 */
	if ((ifp->if_flags & IFF_PPROMISC) == 0 && (m->m_flags & M_PROMISC)) {
		m_freem(m);
		return;
	}

	/*
	 * Reset layer specific mbuf flags to avoid confusing upper layers.
	 * Strip off Ethernet header.
	 */
	m->m_flags &= ~M_VLANTAG;
	m->m_flags &= ~(M_PROTOFLAGS);
	m_adj(m, ETHER_HDR_LEN);

	/*
	 * Dispatch frame to upper layer.
	 */
	switch (ether_type) {
#ifdef INET
	case ETHERTYPE_IP:
#if 0
	    /* FIXME: OSv - port ip fast forward to get perf gain */
	    if ((m = ip_fastforward(m)) == NULL)
			return;
#endif
	    isr = NETISR_IP;
		break;

	case ETHERTYPE_ARP:
		if (ifp->if_flags & IFF_NOARP) {
			/* Discard packet if ARP is disabled on interface */
			m_freem(m);
			return;
		}
		isr = NETISR_ARP;
		break;
#endif
#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		goto discard;
	}
	netisr_dispatch(isr, m);
	return;

discard:

#if 0
	/*
	 * Packet is to be discarded.  If netgraph is present,
	 * hand the packet to it for last chance processing;
	 * otherwise dispose of it.
	 */
	if (IFP2AC(ifp)->ac_netgraph != NULL) {
		KASSERT(ng_ether_input_orphan_p != NULL,
		    ("ng_ether_input_orphan_p is NULL"));
		/*
		 * Put back the ethernet header so netgraph has a
		 * consistent view of inbound packets.
		 */
		M_PREPEND(m, ETHER_HDR_LEN, M_DONTWAIT);
		(*ng_ether_input_orphan_p)(ifp, m);
		return;
	}
#endif
	m_freem(m);
}

/*
 * Convert Ethernet address to printable (loggable) representation.
 * This routine is for compatibility; it's better to just use
 *
 *	printf("%6D", <pointer to address>, ":");
 *
 * since there's no static buffer involved.
 */
char *
ether_sprintf(const u_char *ap)
{
	struct sbuf *buf;
	static char etherbuf[18];
	
	buf = sbuf_new_auto();
	if (buf == NULL)
		return (etherbuf);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"

	sbuf_printf(buf, "%6D", ap, ":");

#pragma GCC diagnostic pop

	sbuf_finish(buf);
	strncpy(etherbuf, sbuf_data(buf), sizeof(etherbuf));
	sbuf_delete(buf);
	return (etherbuf);
}

/*
 * Perform common duties while attaching to interface list
 */
void
ether_ifattach(struct ifnet *ifp, const u_int8_t *lla)
{
	int i;
	struct ifaddr *ifa;
	struct bsd_sockaddr_dl *sdl;

	ifp->if_addrlen = ETHER_ADDR_LEN;
	ifp->if_hdrlen = ETHER_HDR_LEN;
	if_attach(ifp);
	ifp->if_mtu = ETHERMTU;
	ifp->if_output = ether_output;
	ifp->if_input = ether_input;
	ifp->if_resolvemulti = ether_resolvemulti;
	if (ifp->if_baudrate == 0)
		ifp->if_baudrate = IF_Mbps(10);		/* just a default */
	ifp->if_broadcastaddr = etherbroadcastaddr;

	ifa = ifp->if_addr;
	KASSERT(ifa != NULL, ("%s: no lladdr!\n", __func__));
	sdl = (struct bsd_sockaddr_dl *)ifa->ifa_addr;
	sdl->sdl_type = IFT_ETHER;
	sdl->sdl_alen = ifp->if_addrlen;
	bcopy(lla, LLADDR(sdl), ifp->if_addrlen);

#if 0
	bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN);
	if (ng_ether_attach_p != NULL)
		(*ng_ether_attach_p)(ifp);
#endif

	/* Announce Ethernet MAC address if non-zero. */
	for (i = 0; i < ifp->if_addrlen; i++)
		if (lla[i] != 0)
			break; 
	if (i != ifp->if_addrlen)
		printf("%s: ethernet address: %x:%x:%x:%x:%x:%x\n",
		    ifp->if_xname, lla[0], lla[1], lla[2], lla[3], lla[4], lla[5]);
}

/*
 * Perform common duties while detaching an Ethernet interface
 */
void
ether_ifdetach(struct ifnet *ifp)
{
#if 0
	if (IFP2AC(ifp)->ac_netgraph != NULL) {
		KASSERT(ng_ether_detach_p != NULL,
		    ("ng_ether_detach_p is NULL"));
		(*ng_ether_detach_p)(ifp);
	}

	bpfdetach(ifp);
#endif

	if_detach(ifp);
}

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_ETHER, ether, CTLFLAG_RW, 0, "Ethernet");
#if defined(INET) || defined(INET6)
SYSCTL_VNET_INT(_net_link_ether, OID_AUTO, ipfw, CTLFLAG_RW,
	     &VNET_NAME(ether_ipfw), 0, "Pass ether pkts through firewall");
#endif

#if 0
/*
 * This is for reference.  We have a table-driven version
 * of the little-endian crc32 generator, which is faster
 * than the double-loop.
 */
uint32_t
ether_crc32_le(const uint8_t *buf, size_t len)
{
	size_t i;
	uint32_t crc;
	int bit;
	uint8_t data;

	crc = 0xffffffff;	/* initial value */

	for (i = 0; i < len; i++) {
		for (data = *buf++, bit = 0; bit < 8; bit++, data >>= 1) {
			carry = (crc ^ data) & 1;
			crc >>= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_LE);
		}
	}

	return (crc);
}
#else
uint32_t
ether_crc32_le(const uint8_t *buf, size_t len)
{
	static const uint32_t crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	size_t i;
	uint32_t crc;

	crc = 0xffffffff;	/* initial value */

	for (i = 0; i < len; i++) {
		crc ^= buf[i];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}

	return (crc);
}
#endif

uint32_t
ether_crc32_be(const uint8_t *buf, size_t len)
{
	size_t i;
	uint32_t crc, carry;
	int bit;
	uint8_t data;

	crc = 0xffffffff;	/* initial value */

	for (i = 0; i < len; i++) {
		for (data = *buf++, bit = 0; bit < 8; bit++, data >>= 1) {
			carry = ((crc & 0x80000000) ? 1 : 0) ^ (data & 0x01);
			crc <<= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_BE) | carry;
		}
	}

	return (crc);
}

int
ether_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ifaddr *ifa = (struct ifaddr *) data;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0;

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;

		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			ifp->if_init(ifp->if_softc);	/* before arpwhohas */
			arp_ifinit(ifp, ifa);
			break;
#endif
		default:
			ifp->if_init(ifp->if_softc);
			break;
		}
		break;

	case SIOCGIFADDR:
		{
			struct bsd_sockaddr *sa;

			sa = (struct bsd_sockaddr *) & ifr->ifr_data;
			bcopy(IF_LLADDR(ifp),
			      (caddr_t) sa->sa_data, ETHER_ADDR_LEN);
		}
		break;

	case SIOCSIFMTU:
		/*
		 * Set the interface MTU.
		 */
		if (ifr->ifr_mtu > ETHERMTU) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
		}
		break;
	default:
		error = EINVAL;			/* XXX netbsd has ENOTTY??? */
		break;
	}
	return (error);
}

static int
ether_resolvemulti(struct ifnet *ifp, struct bsd_sockaddr **llsa,
	struct bsd_sockaddr *sa)
{
	struct bsd_sockaddr_dl *sdl;
#ifdef INET
	struct bsd_sockaddr_in *sin;
#endif
#ifdef INET6
	struct bsd_sockaddr_in6 *sin6;
#endif
	u_char *e_addr;

	switch(sa->sa_family) {
	case AF_LINK:
		/*
		 * No mapping needed. Just check that it's a valid MC address.
		 */
		sdl = (struct bsd_sockaddr_dl *)sa;
		e_addr = (u_char*)LLADDR(sdl);
		if (!ETHER_IS_MULTICAST(e_addr))
			return EADDRNOTAVAIL;
		*llsa = 0;
		return 0;

#ifdef INET
	case AF_INET:
		sin = (struct bsd_sockaddr_in *)sa;
		if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			return EADDRNOTAVAIL;
		sdl = malloc(sizeof *sdl);
		if (sdl == NULL)
			return ENOMEM;
		bzero(sdl, sizeof *sdl);
		sdl->sdl_len = sizeof *sdl;
		sdl->sdl_family = AF_LINK;
		sdl->sdl_index = ifp->if_index;
		sdl->sdl_type = IFT_ETHER;
		sdl->sdl_alen = ETHER_ADDR_LEN;
		e_addr = (u_char*)LLADDR(sdl);
		ETHER_MAP_IP_MULTICAST(&sin->sin_addr, e_addr);
		*llsa = (struct bsd_sockaddr *)sdl;
		return 0;
#endif
#ifdef INET6
	case AF_INET6:
		sin6 = (struct bsd_sockaddr_in6 *)sa;
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
			/*
			 * An IP6 address of 0 means listen to all
			 * of the Ethernet multicast address used for IP6.
			 * (This is used for multicast routers.)
			 */
			ifp->if_flags |= IFF_ALLMULTI;
			*llsa = 0;
			return 0;
		}
		if (!IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return EADDRNOTAVAIL;
		sdl = malloc(sizeof *sdl, M_IFMADDR,
		       M_NOWAIT|M_ZERO);
		if (sdl == NULL)
			return (ENOMEM);
		sdl->sdl_len = sizeof *sdl;
		sdl->sdl_family = AF_LINK;
		sdl->sdl_index = ifp->if_index;
		sdl->sdl_type = IFT_ETHER;
		sdl->sdl_alen = ETHER_ADDR_LEN;
		e_addr = LLADDR(sdl);
		ETHER_MAP_IPV6_MULTICAST(&sin6->sin6_addr, e_addr);
		*llsa = (struct bsd_sockaddr *)sdl;
		return 0;
#endif

	default:
		/*
		 * Well, the text isn't quite right, but it's the name
		 * that counts...
		 */
		return EAFNOSUPPORT;
	}
}

void*
ether_alloc(u_char type, struct ifnet *ifp)
{
	struct arpcom	*ac;
	
	ac = malloc(sizeof(struct arpcom));
	bzero(ac, sizeof(struct arpcom));
	ac->ac_ifp = ifp;

	return (ac);
}

void
ether_free(void *com, u_char type)
{

	free(com);
}

#if 0
static int
ether_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		if_register_com_alloc(IFT_ETHER, ether_alloc, ether_free);
		break;
	case MOD_UNLOAD:
		if_deregister_com_alloc(IFT_ETHER);
		break;
	default:
		return EOPNOTSUPP;
	}

	return (0);
}

static moduledata_t ether_mod = {
	"ether",
	ether_modevent,
	0
};
#endif

#if 0
void
ether_vlan_mtap(struct bpf_if *bp, struct mbuf *m, void *data, u_int dlen)
{
	struct ether_vlan_header vlan;
	struct mbuf mv, mb;

	KASSERT((m->m_flags & M_VLANTAG) != 0,
	    ("%s: vlan information not present", __func__));
	KASSERT(m->m_len >= sizeof(struct ether_header),
	    ("%s: mbuf not large enough for header", __func__));
	bcopy(mtod(m, char *), &vlan, sizeof(struct ether_header));
	vlan.evl_proto = vlan.evl_encap_proto;
	vlan.evl_encap_proto = htons(ETHERTYPE_VLAN);
	vlan.evl_tag = htons(m->m_pkthdr.ether_vtag);
	m->m_len -= sizeof(struct ether_header);
	m->m_data += sizeof(struct ether_header);
	/*
	 * If a data link has been supplied by the caller, then we will need to
	 * re-create a stack allocated mbuf chain with the following structure:
	 *
	 * (1) mbuf #1 will contain the supplied data link
	 * (2) mbuf #2 will contain the vlan header
	 * (3) mbuf #3 will contain the original mbuf's packet data
	 *
	 * Otherwise, submit the packet and vlan header via bpf_mtap2().
	 */
	if (data != NULL) {
		mv.m_next = m;
		mv.m_data = (caddr_t)&vlan;
		mv.m_len = sizeof(vlan);
		mb.m_next = &mv;
		mb.m_data = data;
		mb.m_len = dlen;
		bpf_mtap(bp, &mb);
	} else
		bpf_mtap2(bp, &vlan, sizeof(vlan), m);
	m->m_len += sizeof(struct ether_header);
	m->m_data -= sizeof(struct ether_header);
}

struct mbuf *
ether_vlanencap(struct mbuf *m, uint16_t tag)
{
	struct ether_vlan_header *evl;

	M_PREPEND(m, ETHER_VLAN_ENCAP_LEN, M_DONTWAIT);
	if (m == NULL)
		return (NULL);
	/* M_PREPEND takes care of m_len, m_pkthdr.len for us */

	if (m->m_len < sizeof(*evl)) {
		m = m_pullup(m, sizeof(*evl));
		if (m == NULL)
			return (NULL);
	}

	/*
	 * Transform the Ethernet header into an Ethernet header
	 * with 802.1Q encapsulation.
	 */
	evl = mtod(m, struct ether_vlan_header *);
	bcopy((char *)evl + ETHER_VLAN_ENCAP_LEN,
	    (char *)evl, ETHER_HDR_LEN - ETHER_TYPE_LEN);
	evl->evl_encap_proto = htons(ETHERTYPE_VLAN);
	evl->evl_tag = htons(tag);
	return (m);
}
#endif

DECLARE_MODULE(ether, ether_mod, SI_SUB_INIT_IF, SI_ORDER_ANY);
MODULE_VERSION(ether, 1);
