/*-
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (C) 2001 WIDE Project.  All rights reserved.
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
 *	@(#)in.c	8.4 (Berkeley) 1/9/95
 */

#include <sys/cdefs.h>

#include <osv/ioctl.h>
#include <osv/trace.hh>
#include <osv/rcu.hh>
#include <functional>

#include <bsd/porting/netport.h>

#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/priv.h>
#include <bsd/sys/sys/socket.h>

#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if_arp.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/net/vnet.h>

#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/netinet/in_pcb.h>
#include <bsd/sys/netinet/ip_var.h>
#include <bsd/sys/netinet/igmp_var.h>

#include <bsd/sys/netinet/udp.h>
#include <bsd/sys/netinet/udp_var.h>
#include <bsd/sys/netinet/arpcache.hh>

static int in_mask2len(struct in_addr *);
static void in_len2mask(struct in_addr *, int);
static int in_lifaddr_ioctl(struct socket *, u_long, caddr_t,
	struct ifnet *, struct thread *);

static int	in_addprefix(struct in_ifaddr *, int);
static int	in_scrubprefix(struct in_ifaddr *, u_int);
static void	in_socktrim(struct bsd_sockaddr_in *);
static int	in_ifinit(struct ifnet *,
	    struct in_ifaddr *, struct bsd_sockaddr_in *, int);
static void	in_purgemaddrs(struct ifnet *);

TRACEPOINT(trace_in_lltable_lookup, "llt=%p, flags=%x", void *, u_int);
TRACEPOINT(trace_in_lltable_lookup_fast, "llt=%p, flags=%x", void *, u_int);

static VNET_DEFINE(int, sameprefixcarponly);
#define	V_sameprefixcarponly		VNET(sameprefixcarponly)
SYSCTL_VNET_INT(_net_inet_ip, OID_AUTO, same_prefix_carp_only, CTLFLAG_RW,
	&VNET_NAME(sameprefixcarponly), 0,
	"Refuse to create same prefixes on different interfaces");

VNET_DECLARE(struct inpcbinfo, ripcbinfo);
#define	V_ripcbinfo			VNET(ripcbinfo)

VNET_DECLARE(struct arpstat, arpstat);  /* ARP statistics, see if_arp.h */
#define	V_arpstat		VNET(arpstat)

/*
 * Return 1 if an internet address is for a ``local'' host
 * (one to which we have a connection).
 */
int
in_localaddr(struct in_addr in)
{
	register u_long i = ntohl(in.s_addr);
	register struct in_ifaddr *ia;

	IN_IFADDR_RLOCK();
	TAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
		if ((i & ia->ia_subnetmask) == ia->ia_subnet) {
			IN_IFADDR_RUNLOCK();
			return (1);
		}
	}
	IN_IFADDR_RUNLOCK();
	return (0);
}

/*
 * Return 1 if an internet address is for the local host and configured
 * on one of its interfaces.
 */
int
in_localip(struct in_addr in)
{
	struct in_ifaddr *ia;

	IN_IFADDR_RLOCK();
	LIST_FOREACH(ia, INADDR_HASH(in.s_addr), ia_hash) {
		if (IA_SIN(ia)->sin_addr.s_addr == in.s_addr) {
			IN_IFADDR_RUNLOCK();
			return (1);
		}
	}
	IN_IFADDR_RUNLOCK();
	return (0);
}

/*
 * Determine whether an IP address is in a reserved set of addresses
 * that may not be forwarded, or whether datagrams to that destination
 * may be forwarded.
 */
int
in_canforward(struct in_addr in)
{
	register u_long i = ntohl(in.s_addr);
	register u_long net;

	if (IN_EXPERIMENTAL(i) || IN_MULTICAST(i) || IN_LINKLOCAL(i))
		return (0);
	if (IN_CLASSA(i)) {
		net = i & IN_CLASSA_NET;
		if (net == 0 || net == (IN_LOOPBACKNET << IN_CLASSA_NSHIFT))
			return (0);
	}
	return (1);
}

/*
 * Trim a mask in a bsd_sockaddr
 */
static void
in_socktrim(struct bsd_sockaddr_in *ap)
{
    register char *cplim = (char *) &ap->sin_addr;
    register char *cp = (char *) (&ap->sin_addr + 1);

    ap->sin_len = 0;
    while (--cp >= cplim)
	if (*cp) {
	    (ap)->sin_len = cp - (char *) (ap) + 1;
	    break;
	}
}

static int
in_mask2len(struct in_addr *mask)
{
	int x, y;
	u_char *p;

	p = (u_char *)mask;
	for (x = 0; x < sizeof(*mask); x++) {
		if (p[x] != 0xff)
			break;
	}
	y = 0;
	if (x < sizeof(*mask)) {
		for (y = 0; y < 8; y++) {
			if ((p[x] & (0x80 >> y)) == 0)
				break;
		}
	}
	return (x * 8 + y);
}

static void
in_len2mask(struct in_addr *mask, int len)
{
	int i;
	u_char *p;

	p = (u_char *)mask;
	bzero(mask, sizeof(*mask));
	for (i = 0; i < len / 8; i++)
		p[i] = 0xff;
	if (len % 8)
		p[i] = (0xff00 >> (len % 8)) & 0xff;
}

/*
 * Generic internet control operations (ioctl's).
 *
 * ifp is NULL if not an interface-specific ioctl.
 */
/* ARGSUSED */
int
in_control(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp,
    struct thread *td)
{
	register struct bsd_ifreq *ifr = (struct bsd_ifreq *)data;
	register struct in_ifaddr *ia, *iap;
	register struct bsd_ifaddr *ifa;
	struct in_addr allhosts_addr;
	struct in_addr dst;
	struct in_ifinfo *ii;
	struct in_aliasreq *ifra = (struct in_aliasreq *)data;
	struct bsd_sockaddr_in oldaddr;
	int error, hostIsNew, iaIsNew, maskIsNew;
	int iaIsFirst;

	ia = NULL;
	iaIsFirst = 0;
	iaIsNew = 0;
	allhosts_addr.s_addr = htonl(INADDR_ALLHOSTS_GROUP);

	/*
	 * Filter out ioctls we implement directly; forward the rest on to
	 * in_lifaddr_ioctl() and ifp->if_ioctl().
	 */
	switch (cmd) {
	case SIOCAIFADDR:
	case SIOCDIFADDR:
	case SIOCGIFADDR:
	case SIOCGIFBRDADDR:
	case SIOCGIFDSTADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFADDR:
	case SIOCSIFBRDADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFNETMASK:
		break;

	case SIOCALIFADDR:
		if (td != NULL) {
			error = priv_check(td, PRIV_NET_ADDIFADDR);
			if (error)
				return (error);
		}
		if (ifp == NULL)
			return (EINVAL);
		return in_lifaddr_ioctl(so, cmd, data, ifp, td);

	case SIOCDLIFADDR:
		if (td != NULL) {
			error = priv_check(td, PRIV_NET_DELIFADDR);
			if (error)
				return (error);
		}
		if (ifp == NULL)
			return (EINVAL);
		return in_lifaddr_ioctl(so, cmd, data, ifp, td);

	case SIOCGLIFADDR:
		if (ifp == NULL)
			return (EINVAL);
		return in_lifaddr_ioctl(so, cmd, data, ifp, td);

	default:
		if (ifp == NULL || ifp->if_ioctl == NULL)
			return (EOPNOTSUPP);
		return ((*ifp->if_ioctl)(ifp, cmd, data));
	}

	if (ifp == NULL)
		return (EADDRNOTAVAIL);

	/*
	 * Security checks before we get involved in any work.
	 */
	switch (cmd) {
	case SIOCAIFADDR:
	case SIOCSIFADDR:
	case SIOCSIFBRDADDR:
	case SIOCSIFNETMASK:
	case SIOCSIFDSTADDR:
		if (td != NULL) {
			error = priv_check(td, PRIV_NET_ADDIFADDR);
			if (error)
				return (error);
		}
		break;

	case SIOCDIFADDR:
		if (td != NULL) {
			error = priv_check(td, PRIV_NET_DELIFADDR);
			if (error)
				return (error);
		}
		break;
	}

	/*
	 * Find address for this interface, if it exists.
	 *
	 * If an alias address was specified, find that one instead of the
	 * first one on the interface, if possible.
	 */
	dst = ((struct bsd_sockaddr_in *)&ifr->ifr_addr)->sin_addr;
	IN_IFADDR_RLOCK();
	LIST_FOREACH(iap, INADDR_HASH(dst.s_addr), ia_hash) {
		if (iap->ia_ifp == ifp &&
		    iap->ia_addr.sin_addr.s_addr == dst.s_addr) {
			ia = iap;
			break;
		}
	}
	if (ia != NULL)
		ifa_ref(&ia->ia_ifa);
	IN_IFADDR_RUNLOCK();
	if (ia == NULL) {
		IF_ADDR_RLOCK(ifp);
		TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			iap = ifatoia(ifa);
			if (iap->ia_addr.sin_family == AF_INET) {
				ia = iap;
				break;
			}
		}
		if (ia != NULL)
			ifa_ref(&ia->ia_ifa);
		IF_ADDR_RUNLOCK(ifp);
	}
	if (ia == NULL)
		iaIsFirst = 1;

	error = 0;
	switch (cmd) {
	case SIOCAIFADDR:
	case SIOCDIFADDR:
		if (ifra->ifra_addr.sin_family == AF_INET) {
			struct in_ifaddr *oia;

			IN_IFADDR_RLOCK();
			for (oia = ia; ia; ia = TAILQ_NEXT(ia, ia_link)) {
				if (ia->ia_ifp == ifp  &&
				    ia->ia_addr.sin_addr.s_addr ==
				    ifra->ifra_addr.sin_addr.s_addr)
					break;
			}
			if (ia != NULL && ia != oia)
				ifa_ref(&ia->ia_ifa);
			if (oia != NULL && ia != oia)
				ifa_free(&oia->ia_ifa);
			IN_IFADDR_RUNLOCK();
			if ((ifp->if_flags & IFF_POINTOPOINT)
			    && (cmd == SIOCAIFADDR)
			    && (ifra->ifra_dstaddr.sin_addr.s_addr
				== INADDR_ANY)) {
				error = EDESTADDRREQ;
				goto out;
			}
		}
		if (cmd == SIOCDIFADDR && ia == NULL) {
			error = EADDRNOTAVAIL;
			goto out;
		}
		/* FALLTHROUGH */
	case SIOCSIFADDR:
	case SIOCSIFNETMASK:
	case SIOCSIFDSTADDR:
		if (ia == NULL) {
			ia = (struct in_ifaddr *)malloc(sizeof *ia);
			if (ia == NULL) {
				error = ENOBUFS;
				goto out;
			}

			bzero(ia, sizeof(*ia));

			ifa = &ia->ia_ifa;
			ifa_init(ifa);
			ifa->ifa_addr = (struct bsd_sockaddr *)&ia->ia_addr;
			ifa->ifa_dstaddr = (struct bsd_sockaddr *)&ia->ia_dstaddr;
			ifa->ifa_netmask = (struct bsd_sockaddr *)&ia->ia_sockmask;

			ia->ia_sockmask.sin_len = 8;
			ia->ia_sockmask.sin_family = AF_INET;
			if (ifp->if_flags & IFF_BROADCAST) {
				ia->ia_broadaddr.sin_len = sizeof(ia->ia_addr);
				ia->ia_broadaddr.sin_family = AF_INET;
			}
			ia->ia_ifp = ifp;

			ifa_ref(ifa);			/* if_addrhead */
			IF_ADDR_WLOCK(ifp);
			TAILQ_INSERT_TAIL(&ifp->if_addrhead, ifa, ifa_link);
			IF_ADDR_WUNLOCK(ifp);
			ifa_ref(ifa);			/* in_ifaddrhead */
			IN_IFADDR_WLOCK();
			TAILQ_INSERT_TAIL(&V_in_ifaddrhead, ia, ia_link);
			IN_IFADDR_WUNLOCK();
			iaIsNew = 1;
		}
		break;

	case SIOCSIFBRDADDR:
	case SIOCGIFADDR:
	case SIOCGIFNETMASK:
	case SIOCGIFDSTADDR:
	case SIOCGIFBRDADDR:
		if (ia == NULL) {
			error = EADDRNOTAVAIL;
			goto out;
		}
		break;
	}

	/*
	 * Most paths in this switch return directly or via out.  Only paths
	 * that remove the address break in order to hit common removal code.
	 */
	switch (cmd) {
	case SIOCGIFADDR:
		*((struct bsd_sockaddr_in *)&ifr->ifr_addr) = ia->ia_addr;
		goto out;

	case SIOCGIFBRDADDR:
#if 0
		if ((ifp->if_flags & IFF_BROADCAST) == 0) {
			error = EINVAL;
			goto out;
		}
#endif
		*((struct bsd_sockaddr_in *)&ifr->ifr_broadaddr) = ia->ia_broadaddr;
		goto out;

	case SIOCGIFDSTADDR:
		if ((ifp->if_flags & IFF_POINTOPOINT) == 0) {
			error = EINVAL;
			goto out;
		}
		*((struct bsd_sockaddr_in *)&ifr->ifr_dstaddr) = ia->ia_dstaddr;
		goto out;

	case SIOCGIFNETMASK:
		*((struct bsd_sockaddr_in *)&ifr->ifr_addr) = ia->ia_sockmask;
		goto out;

	case SIOCSIFDSTADDR:
		if ((ifp->if_flags & IFF_POINTOPOINT) == 0) {
			error = EINVAL;
			goto out;
		}
		oldaddr = ia->ia_dstaddr;
		ia->ia_dstaddr = *(struct bsd_sockaddr_in *)&ifr->ifr_dstaddr;
		if (ifp->if_ioctl != NULL) {
			error = (*ifp->if_ioctl)(ifp, SIOCSIFDSTADDR,
			    (caddr_t)ia);
			if (error) {
				ia->ia_dstaddr = oldaddr;
				goto out;
			}
		}
		if (ia->ia_flags & IFA_ROUTE) {
			ia->ia_ifa.ifa_dstaddr = (struct bsd_sockaddr *)&oldaddr;
			rtinit(&(ia->ia_ifa), (int)RTM_DELETE, RTF_HOST);
			ia->ia_ifa.ifa_dstaddr =
					(struct bsd_sockaddr *)&ia->ia_dstaddr;
			rtinit(&(ia->ia_ifa), (int)RTM_ADD, RTF_HOST|RTF_UP);
		}
		goto out;

	case SIOCSIFBRDADDR:
		if ((ifp->if_flags & IFF_BROADCAST) == 0) {
			error = EINVAL;
			goto out;
		}
		ia->ia_broadaddr = *(struct bsd_sockaddr_in *)&ifr->ifr_broadaddr;
		goto out;

	case SIOCSIFADDR:
		error = in_ifinit(ifp, ia,
		    (struct bsd_sockaddr_in *) &ifr->ifr_addr, 1);
		if (error != 0 && iaIsNew)
			break;
		if (error == 0) {
			ii = ((struct in_ifinfo *)ifp->if_afdata[AF_INET]);
			if (iaIsFirst &&
			    (ifp->if_flags & IFF_MULTICAST) != 0) {
				error = in_joingroup(ifp, &allhosts_addr,
				    NULL, &ii->ii_allhosts);
			}
			EVENTHANDLER_INVOKE(ifaddr_event, ifp);
		}
		error = 0;
		goto out;

	case SIOCSIFNETMASK:
		ia->ia_sockmask.sin_addr = ifra->ifra_addr.sin_addr;
		ia->ia_subnetmask = ntohl(ia->ia_sockmask.sin_addr.s_addr);
		goto out;

	case SIOCAIFADDR:
		maskIsNew = 0;
		hostIsNew = 1;
		error = 0;
		if (ia->ia_addr.sin_family == AF_INET) {
			if (ifra->ifra_addr.sin_len == 0) {
				ifra->ifra_addr = ia->ia_addr;
				hostIsNew = 0;
			} else if (ifra->ifra_addr.sin_addr.s_addr ==
					       ia->ia_addr.sin_addr.s_addr)
				hostIsNew = 0;
		}
		if (ifra->ifra_mask.sin_len) {
			/*
			 * QL: XXX
			 * Need to scrub the prefix here in case
			 * the issued command is SIOCAIFADDR with
			 * the same address, but with a different
			 * prefix length. And if the prefix length
			 * is the same as before, then the call is
			 * un-necessarily executed here.
			 */
			in_ifscrub(ifp, ia, LLE_STATIC);
			ia->ia_sockmask = ifra->ifra_mask;
			ia->ia_sockmask.sin_family = AF_INET;
			ia->ia_subnetmask =
			    ntohl(ia->ia_sockmask.sin_addr.s_addr);
			maskIsNew = 1;
		}
		if ((ifp->if_flags & IFF_POINTOPOINT) &&
		    (ifra->ifra_dstaddr.sin_family == AF_INET)) {
			in_ifscrub(ifp, ia, LLE_STATIC);
			ia->ia_dstaddr = ifra->ifra_dstaddr;
			maskIsNew  = 1; /* We lie; but the effect's the same */
		}
		if (ifra->ifra_addr.sin_family == AF_INET &&
		    (hostIsNew || maskIsNew))
			error = in_ifinit(ifp, ia, &ifra->ifra_addr, 0);
		if (error != 0 && iaIsNew)
			break;

		if ((ifp->if_flags & IFF_BROADCAST) &&
		    (ifra->ifra_broadaddr.sin_family == AF_INET))
			ia->ia_broadaddr = ifra->ifra_broadaddr;
		if (error == 0) {
			ii = ((struct in_ifinfo *)ifp->if_afdata[AF_INET]);
			if (iaIsFirst &&
			    (ifp->if_flags & IFF_MULTICAST) != 0) {
				error = in_joingroup(ifp, &allhosts_addr,
				    NULL, &ii->ii_allhosts);
			}
			EVENTHANDLER_INVOKE(ifaddr_event, ifp);
		}
		goto out;

	case SIOCDIFADDR:
		/*
		 * in_ifscrub kills the interface route.
		 */
		in_ifscrub(ifp, ia, LLE_STATIC);

		/*
		 * in_ifadown gets rid of all the rest of
		 * the routes.  This is not quite the right
		 * thing to do, but at least if we are running
		 * a routing process they will come back.
		 */
		in_ifadown(&ia->ia_ifa, 1);
		EVENTHANDLER_INVOKE(ifaddr_event, ifp);
		error = 0;
		break;

	default:
		panic("in_control: unsupported ioctl");
	}

	IF_ADDR_WLOCK(ifp);
	/* Re-check that ia is still part of the list. */
	TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa == &ia->ia_ifa)
			break;
	}
	if (ifa == NULL) {
		/*
		 * If we lost the race with another thread, there is no need to
		 * try it again for the next loop as there is no other exit
		 * path between here and out.
		 */
		IF_ADDR_WUNLOCK(ifp);
		error = EADDRNOTAVAIL;
		goto out;
	}
	TAILQ_REMOVE(&ifp->if_addrhead, &ia->ia_ifa, ifa_link);
	IF_ADDR_WUNLOCK(ifp);
	ifa_free(&ia->ia_ifa);				/* if_addrhead */

	IN_IFADDR_WLOCK();
	TAILQ_REMOVE(&V_in_ifaddrhead, ia, ia_link);
	if (ia->ia_addr.sin_family == AF_INET) {
		struct in_ifaddr *if_ia;

		LIST_REMOVE(ia, ia_hash);
		IN_IFADDR_WUNLOCK();
		/*
		 * If this is the last IPv4 address configured on this
		 * interface, leave the all-hosts group.
		 * No state-change report need be transmitted.
		 */
		if_ia = NULL;
		IFP_TO_IA(ifp, if_ia);
		if (if_ia == NULL) {
			ii = ((struct in_ifinfo *)ifp->if_afdata[AF_INET]);
			IN_MULTI_LOCK();
			if (ii->ii_allhosts) {
				(void)in_leavegroup_locked(ii->ii_allhosts,
				    NULL);
				ii->ii_allhosts = NULL;
			}
			IN_MULTI_UNLOCK();
		} else
			ifa_free(&if_ia->ia_ifa);
	} else
		IN_IFADDR_WUNLOCK();
	ifa_free(&ia->ia_ifa);				/* in_ifaddrhead */
out:
	if (ia != NULL)
		ifa_free(&ia->ia_ifa);
	return (error);
}

/*
 * SIOC[GAD]LIFADDR.
 *	SIOCGLIFADDR: get first address. (?!?)
 *	SIOCGLIFADDR with IFLR_PREFIX:
 *		get first address that matches the specified prefix.
 *	SIOCALIFADDR: add the specified address.
 *	SIOCALIFADDR with IFLR_PREFIX:
 *		EINVAL since we can't deduce hostid part of the address.
 *	SIOCDLIFADDR: delete the specified address.
 *	SIOCDLIFADDR with IFLR_PREFIX:
 *		delete the first address that matches the specified prefix.
 * return values:
 *	EINVAL on invalid parameters
 *	EADDRNOTAVAIL on prefix match failed/specified address not found
 *	other values may be returned from in_ioctl()
 */
static int
in_lifaddr_ioctl(struct socket *so, u_long cmd, caddr_t data,
    struct ifnet *ifp, struct thread *td)
{
	struct if_laddrreq *iflr = (struct if_laddrreq *)data;
	struct bsd_ifaddr *ifa;

	/* sanity checks */
	if (data == NULL || ifp == NULL) {
		panic("invalid argument to in_lifaddr_ioctl");
		/*NOTRECHED*/
	}

	switch (cmd) {
	case SIOCGLIFADDR:
		/* address must be specified on GET with IFLR_PREFIX */
		if ((iflr->flags & IFLR_PREFIX) == 0)
			break;
		/*FALLTHROUGH*/
	case SIOCALIFADDR:
	case SIOCDLIFADDR:
		/* address must be specified on ADD and DELETE */
		if (iflr->addr.ss_family != AF_INET)
			return (EINVAL);
		if (iflr->addr.ss_len != sizeof(struct bsd_sockaddr_in))
			return (EINVAL);
		/* XXX need improvement */
		if (iflr->dstaddr.ss_family
		 && iflr->dstaddr.ss_family != AF_INET)
			return (EINVAL);
		if (iflr->dstaddr.ss_family
		 && iflr->dstaddr.ss_len != sizeof(struct bsd_sockaddr_in))
			return (EINVAL);
		break;
	default: /*shouldn't happen*/
		return (EOPNOTSUPP);
	}
	if (sizeof(struct in_addr) * 8 < iflr->prefixlen)
		return (EINVAL);

	switch (cmd) {
	case SIOCALIFADDR:
	    {
		struct in_aliasreq ifra;

		if (iflr->flags & IFLR_PREFIX)
			return (EINVAL);

		/* copy args to in_aliasreq, perform ioctl(SIOCAIFADDR). */
		bzero(&ifra, sizeof(ifra));
		bcopy(iflr->iflr_name, ifra.ifra_name,
			sizeof(ifra.ifra_name));

		bcopy(&iflr->addr, &ifra.ifra_addr, iflr->addr.ss_len);

		if (iflr->dstaddr.ss_family) {	/*XXX*/
			bcopy(&iflr->dstaddr, &ifra.ifra_dstaddr,
				iflr->dstaddr.ss_len);
		}

		ifra.ifra_mask.sin_family = AF_INET;
		ifra.ifra_mask.sin_len = sizeof(struct bsd_sockaddr_in);
		in_len2mask(&ifra.ifra_mask.sin_addr, iflr->prefixlen);

		return (in_control(so, SIOCAIFADDR, (caddr_t)&ifra, ifp, td));
	    }
	case SIOCGLIFADDR:
	case SIOCDLIFADDR:
	    {
		struct in_ifaddr *ia;
		struct in_addr mask, candidate, match;
		struct bsd_sockaddr_in *sin;

		bzero(&mask, sizeof(mask));
		bzero(&match, sizeof(match));
		if (iflr->flags & IFLR_PREFIX) {
			/* lookup a prefix rather than address. */
			in_len2mask(&mask, iflr->prefixlen);

			sin = (struct bsd_sockaddr_in *)&iflr->addr;
			match.s_addr = sin->sin_addr.s_addr;
			match.s_addr &= mask.s_addr;

			/* if you set extra bits, that's wrong */
			if (match.s_addr != sin->sin_addr.s_addr)
				return (EINVAL);

		} else {
			/* on getting an address, take the 1st match */
			/* on deleting an address, do exact match */
			if (cmd != SIOCGLIFADDR) {
				in_len2mask(&mask, 32);
				sin = (struct bsd_sockaddr_in *)&iflr->addr;
				match.s_addr = sin->sin_addr.s_addr;
			}
		}

		IF_ADDR_RLOCK(ifp);
		TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)	{
			if (ifa->ifa_addr->sa_family != AF_INET)
				continue;
			if (match.s_addr == 0)
				break;
			candidate.s_addr = ((struct bsd_sockaddr_in *)&ifa->ifa_addr)->sin_addr.s_addr;
			candidate.s_addr &= mask.s_addr;
			if (candidate.s_addr == match.s_addr)
				break;
		}
		if (ifa != NULL)
			ifa_ref(ifa);
		IF_ADDR_RUNLOCK(ifp);
		if (ifa == NULL)
			return (EADDRNOTAVAIL);
		ia = (struct in_ifaddr *)ifa;

		if (cmd == SIOCGLIFADDR) {
			/* fill in the if_laddrreq structure */
			bcopy(&ia->ia_addr, &iflr->addr, ia->ia_addr.sin_len);

			if ((ifp->if_flags & IFF_POINTOPOINT) != 0) {
				bcopy(&ia->ia_dstaddr, &iflr->dstaddr,
					ia->ia_dstaddr.sin_len);
			} else
				bzero(&iflr->dstaddr, sizeof(iflr->dstaddr));

			iflr->prefixlen =
				in_mask2len(&ia->ia_sockmask.sin_addr);

			iflr->flags = 0;	/*XXX*/
			ifa_free(ifa);

			return (0);
		} else {
			struct in_aliasreq ifra;

			/* fill in_aliasreq and do ioctl(SIOCDIFADDR) */
			bzero(&ifra, sizeof(ifra));
			bcopy(iflr->iflr_name, ifra.ifra_name,
				sizeof(ifra.ifra_name));

			bcopy(&ia->ia_addr, &ifra.ifra_addr,
				ia->ia_addr.sin_len);
			if ((ifp->if_flags & IFF_POINTOPOINT) != 0) {
				bcopy(&ia->ia_dstaddr, &ifra.ifra_dstaddr,
					ia->ia_dstaddr.sin_len);
			}
			bcopy(&ia->ia_sockmask, &ifra.ifra_dstaddr,
				ia->ia_sockmask.sin_len);
			ifa_free(ifa);

			return (in_control(so, SIOCDIFADDR, (caddr_t)&ifra,
			    ifp, td));
		}
	    }
	}

	return (EOPNOTSUPP);	/*just for safety*/
}

/*
 * Delete any existing route for an interface.
 */
void
in_ifscrub(struct ifnet *ifp, struct in_ifaddr *ia, u_int flags)
{

	in_scrubprefix(ia, flags);
}

/*
 * Initialize an interface's internet address
 * and routing table entry.
 */
static int
in_ifinit(struct ifnet *ifp, struct in_ifaddr *ia, struct bsd_sockaddr_in *sin,
    int scrub)
{
	register u_long i = ntohl(sin->sin_addr.s_addr);
	struct bsd_sockaddr_in oldaddr;
	int s = splimp(), flags = RTF_UP, error = 0;

	oldaddr = ia->ia_addr;
	if (oldaddr.sin_family == AF_INET)
		LIST_REMOVE(ia, ia_hash);
	ia->ia_addr = *sin;
	if (ia->ia_addr.sin_family == AF_INET) {
		IN_IFADDR_WLOCK();
		LIST_INSERT_HEAD(INADDR_HASH(ia->ia_addr.sin_addr.s_addr),
		    ia, ia_hash);
		IN_IFADDR_WUNLOCK();
	}
	/*
	 * Give the interface a chance to initialize
	 * if this is its first address,
	 * and to validate the address if necessary.
	 */
	if (ifp->if_ioctl != NULL) {
		error = (*ifp->if_ioctl)(ifp, SIOCSIFADDR, (caddr_t)ia);
		if (error) {
			splx(s);
			/* LIST_REMOVE(ia, ia_hash) is done in in_control */
			ia->ia_addr = oldaddr;
			IN_IFADDR_WLOCK();
			if (ia->ia_addr.sin_family == AF_INET)
				LIST_INSERT_HEAD(INADDR_HASH(
				    ia->ia_addr.sin_addr.s_addr), ia, ia_hash);
			else 
				/* 
				 * If oldaddr family is not AF_INET (e.g. 
				 * interface has been just created) in_control 
				 * does not call LIST_REMOVE, and we end up 
				 * with bogus ia entries in hash
				 */
				LIST_REMOVE(ia, ia_hash);
			IN_IFADDR_WUNLOCK();
			return (error);
		}
	}
	splx(s);
	if (scrub) {
		ia->ia_ifa.ifa_addr = (struct bsd_sockaddr *)&oldaddr;
		in_ifscrub(ifp, ia, LLE_STATIC);
		ia->ia_ifa.ifa_addr = (struct bsd_sockaddr *)&ia->ia_addr;
	}
	/*
	 * Be compatible with network classes, if netmask isn't supplied,
	 * guess it based on classes.
	 */
	if (ia->ia_subnetmask == 0) {
		if (IN_CLASSA(i))
			ia->ia_subnetmask = IN_CLASSA_NET;
		else if (IN_CLASSB(i))
			ia->ia_subnetmask = IN_CLASSB_NET;
		else
			ia->ia_subnetmask = IN_CLASSC_NET;
		ia->ia_sockmask.sin_addr.s_addr = htonl(ia->ia_subnetmask);
	}
	ia->ia_subnet = i & ia->ia_subnetmask;
	in_socktrim(&ia->ia_sockmask);
	/*
	 * XXX: carp(4) does not have interface route
	 */
	if (ifp->if_type == IFT_CARP)
		return (0);
	/*
	 * Add route for the network.
	 */
	ia->ia_ifa.ifa_metric = ifp->if_metric;
	if (ifp->if_flags & IFF_BROADCAST) {
		if (ia->ia_subnetmask == IN_RFC3021_MASK)
			ia->ia_broadaddr.sin_addr.s_addr = INADDR_BROADCAST;
		else
			ia->ia_broadaddr.sin_addr.s_addr =
			    htonl(ia->ia_subnet | ~ia->ia_subnetmask);
	} else if (ifp->if_flags & IFF_LOOPBACK) {
		ia->ia_dstaddr = ia->ia_addr;
		flags |= RTF_HOST;
	} else if (ifp->if_flags & IFF_POINTOPOINT) {
		if (ia->ia_dstaddr.sin_family != AF_INET)
			return (0);
		flags |= RTF_HOST;
	}
	if ((error = in_addprefix(ia, flags)) != 0)
		return (error);

	if (ia->ia_addr.sin_addr.s_addr == INADDR_ANY)
		return (0);

	if (ifp->if_flags & IFF_POINTOPOINT) {
		if (ia->ia_dstaddr.sin_addr.s_addr == ia->ia_addr.sin_addr.s_addr)
			return (0);
	}


	/*
	 * add a loopback route to self
	 */
	if (V_useloopback && !(ifp->if_flags & IFF_LOOPBACK)) {
		struct route ia_ro;

		bzero(&ia_ro, sizeof(ia_ro));
		*((struct bsd_sockaddr_in *)(&ia_ro.ro_dst)) = ia->ia_addr;
		rtalloc_ign_fib(&ia_ro, 0, RT_DEFAULT_FIB);
		if ((ia_ro.ro_rt != NULL) && (ia_ro.ro_rt->rt_ifp != NULL) &&
		    (ia_ro.ro_rt->rt_ifp == V_loif)) {
			RT_LOCK(ia_ro.ro_rt);
			RT_ADDREF(ia_ro.ro_rt);
			RTFREE_LOCKED(ia_ro.ro_rt);
		} else
			error = ifa_add_loopback_route((struct bsd_ifaddr *)ia,
			    (struct bsd_sockaddr *)&ia->ia_addr);
		if (error == 0)
			ia->ia_flags |= IFA_RTSELF;
		if (ia_ro.ro_rt != NULL)
			RTFREE(ia_ro.ro_rt);
	}

	return (error);
}

#define rtinitflags(x) \
	((((x)->ia_ifp->if_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)) != 0) \
	    ? RTF_HOST : 0)

/*
 * Generate a routing message when inserting or deleting
 * an interface address alias.
 */
static void in_addralias_rtmsg(int cmd, struct in_addr *prefix,
    struct in_ifaddr *target)
{
	struct route pfx_ro;
	struct bsd_sockaddr_in *pfx_addr;
	struct rtentry msg_rt;

	/* QL: XXX
	 * This is a bit questionable because there is no
	 * additional route entry added/deleted for an address
	 * alias. Therefore this route report is inaccurate.
	 */
	bzero(&pfx_ro, sizeof(pfx_ro));
	pfx_addr = (struct bsd_sockaddr_in *)(&pfx_ro.ro_dst);
	pfx_addr->sin_len = sizeof(*pfx_addr);
	pfx_addr->sin_family = AF_INET;
	pfx_addr->sin_addr = *prefix;
	rtalloc_ign_fib(&pfx_ro, 0, 0);
	if (pfx_ro.ro_rt != NULL) {
		// cannot directly assign because the embedded mtx is not assignable
		memcpy(&msg_rt, pfx_ro.ro_rt, sizeof(msg_rt));

		/* QL: XXX
		 * Point the gateway to the new interface
		 * address as if a new prefix route entry has
		 * been added through the new address alias.
		 * All other parts of the rtentry is accurate,
		 * e.g., rt_key, rt_mask, rt_ifp etc.
		 */
		msg_rt.rt_gateway = (struct bsd_sockaddr *)&target->ia_addr;
		rt_newaddrmsg(cmd, (struct bsd_ifaddr *)target, 0, &msg_rt);
		RTFREE(pfx_ro.ro_rt);
	}
	return;
}

/*
 * Check if we have a route for the given prefix already or add one accordingly.
 */
static int
in_addprefix(struct in_ifaddr *target, int flags)
{
	struct in_ifaddr *ia;
	struct in_addr prefix, mask, p, m;
	int error;

	if ((flags & RTF_HOST) != 0) {
		prefix = target->ia_dstaddr.sin_addr;
		mask.s_addr = 0;
	} else {
		prefix = target->ia_addr.sin_addr;
		mask = target->ia_sockmask.sin_addr;
		prefix.s_addr &= mask.s_addr;
	}

	IN_IFADDR_RLOCK();
	TAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
		if (rtinitflags(ia)) {
			p = ia->ia_dstaddr.sin_addr;

			if (prefix.s_addr != p.s_addr)
				continue;
		} else {
			p = ia->ia_addr.sin_addr;
			m = ia->ia_sockmask.sin_addr;
			p.s_addr &= m.s_addr;

			if (prefix.s_addr != p.s_addr ||
			    mask.s_addr != m.s_addr)
				continue;
		}

		/*
		 * If we got a matching prefix route inserted by other
		 * interface address, we are done here.
		 */
		if (ia->ia_flags & IFA_ROUTE) {
#ifdef RADIX_MPATH
			if (ia->ia_addr.sin_addr.s_addr ==
			    target->ia_addr.sin_addr.s_addr) {
				IN_IFADDR_RUNLOCK();
				return (EEXIST);
			} else
				break;
#endif
			if (V_sameprefixcarponly &&
			    target->ia_ifp->if_type != IFT_CARP &&
			    ia->ia_ifp->if_type != IFT_CARP) {
				IN_IFADDR_RUNLOCK();
				return (EEXIST);
			} else {
				in_addralias_rtmsg(RTM_ADD, &prefix, target);
				IN_IFADDR_RUNLOCK();
				return (0);
			}
		}
	}
	IN_IFADDR_RUNLOCK();

	/*
	 * No-one seem to have this prefix route, so we try to insert it.
	 */
	error = rtinit(&target->ia_ifa, (int)RTM_ADD, flags);
	if (!error)
		target->ia_flags |= IFA_ROUTE;
	return (error);
}

extern void arp_ifscrub(struct ifnet *ifp, uint32_t addr);

/*
 * If there is no other address in the system that can serve a route to the
 * same prefix, remove the route.  Hand over the route to the new address
 * otherwise.
 */
static int
in_scrubprefix(struct in_ifaddr *target, u_int flags)
{
	struct in_ifaddr *ia;
	struct in_addr prefix, mask, p;
	int error = 0;
	struct bsd_sockaddr_in prefix0, mask0;

	/*
	 * Remove the loopback route to the interface address.
	 * The "useloopback" setting is not consulted because if the
	 * user configures an interface address, turns off this
	 * setting, and then tries to delete that interface address,
	 * checking the current setting of "useloopback" would leave
	 * that interface address loopback route untouched, which
	 * would be wrong. Therefore the interface address loopback route
	 * deletion is unconditional.
	 */
	if ((target->ia_addr.sin_addr.s_addr != INADDR_ANY) &&
	    !(target->ia_ifp->if_flags & IFF_LOOPBACK) &&
	    (target->ia_flags & IFA_RTSELF)) {
		struct route ia_ro;
		int freeit = 0;

		bzero(&ia_ro, sizeof(ia_ro));
		*((struct bsd_sockaddr_in *)(&ia_ro.ro_dst)) = target->ia_addr;
		rtalloc_ign_fib(&ia_ro, 0, 0);
		if ((ia_ro.ro_rt != NULL) && (ia_ro.ro_rt->rt_ifp != NULL) &&
		    (ia_ro.ro_rt->rt_ifp == V_loif)) {
			RT_LOCK(ia_ro.ro_rt);
			if (ia_ro.ro_rt->rt_refcnt <= 1)
				freeit = 1;
			else if (flags & LLE_STATIC) {
				RT_REMREF(ia_ro.ro_rt);
				target->ia_flags &= ~IFA_RTSELF;
			}
			RTFREE_LOCKED(ia_ro.ro_rt);
		}
		if (freeit && (flags & LLE_STATIC)) {
			error = ifa_del_loopback_route((struct bsd_ifaddr *)target,
			    (struct bsd_sockaddr *)&target->ia_addr);
			if (error == 0)
				target->ia_flags &= ~IFA_RTSELF;
		}
		if ((flags & LLE_STATIC) &&
			!(target->ia_ifp->if_flags & IFF_NOARP))
			/* remove arp cache */
			arp_ifscrub(target->ia_ifp, IA_SIN(target)->sin_addr.s_addr);
	}

	if (rtinitflags(target))
		prefix = target->ia_dstaddr.sin_addr;
	else {
		prefix = target->ia_addr.sin_addr;
		mask = target->ia_sockmask.sin_addr;
		prefix.s_addr &= mask.s_addr;
	}

	if ((target->ia_flags & IFA_ROUTE) == 0) {
		in_addralias_rtmsg(RTM_DELETE, &prefix, target);
		return (0);
	}

	IN_IFADDR_RLOCK();
	TAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
		if (rtinitflags(ia))
			p = ia->ia_dstaddr.sin_addr;
		else {
			p = ia->ia_addr.sin_addr;
			p.s_addr &= ia->ia_sockmask.sin_addr.s_addr;
		}

		if ((prefix.s_addr != p.s_addr) ||
		    !(ia->ia_ifp->if_flags & IFF_UP))
			continue;

		/*
		 * If we got a matching prefix address, move IFA_ROUTE and
		 * the route itself to it.  Make sure that routing daemons
		 * get a heads-up.
		 *
		 * XXX: a special case for carp(4) interface - this should
		 *      be more generally specified as an interface that
		 *      doesn't support such action.
		 */
		if ((ia->ia_flags & IFA_ROUTE) == 0
		    && (ia->ia_ifp->if_type != IFT_CARP)) {
			ifa_ref(&ia->ia_ifa);
			IN_IFADDR_RUNLOCK();
			error = rtinit(&(target->ia_ifa), (int)RTM_DELETE,
			    rtinitflags(target));
			if (error == 0)
				target->ia_flags &= ~IFA_ROUTE;
			else
				bsd_log(LOG_INFO, "in_scrubprefix: err=%d, old prefix delete failed\n",
					error);
			error = rtinit(&ia->ia_ifa, (int)RTM_ADD,
			    rtinitflags(ia) | RTF_UP);
			if (error == 0)
				ia->ia_flags |= IFA_ROUTE;
			else
				bsd_log(LOG_INFO, "in_scrubprefix: err=%d, new prefix add failed\n",
					error);
			ifa_free(&ia->ia_ifa);
			return (error);
		}
	}
	IN_IFADDR_RUNLOCK();

	/*
	 * remove all L2 entries on the given prefix
	 */
	bzero(&prefix0, sizeof(prefix0));
	prefix0.sin_len = sizeof(prefix0);
	prefix0.sin_family = AF_INET;
	prefix0.sin_addr.s_addr = target->ia_subnet;
	bzero(&mask0, sizeof(mask0));
	mask0.sin_len = sizeof(mask0);
	mask0.sin_family = AF_INET;
	mask0.sin_addr.s_addr = target->ia_subnetmask;
	lltable_prefix_free(AF_INET, (struct bsd_sockaddr *)&prefix0,
	    (struct bsd_sockaddr *)&mask0, flags);

	/*
	 * As no-one seem to have this prefix, we can remove the route.
	 */
	error = rtinit(&(target->ia_ifa), (int)RTM_DELETE, rtinitflags(target));
	if (error == 0)
		target->ia_flags &= ~IFA_ROUTE;
	else
		bsd_log(LOG_INFO, "in_scrubprefix: err=%d, prefix delete failed\n", error);
	return (error);
}

#undef rtinitflags

/*
 * Return 1 if the address might be a local broadcast address.
 */
int
in_broadcast(struct in_addr in, struct ifnet *ifp)
{
	register struct bsd_ifaddr *ifa;
	u_long t;

	if (in.s_addr == INADDR_BROADCAST ||
	    in.s_addr == INADDR_ANY)
		return (1);
	if ((ifp->if_flags & IFF_BROADCAST) == 0)
		return (0);
	t = ntohl(in.s_addr);
	/*
	 * Look through the list of addresses for a match
	 * with a broadcast address.
	 */
#define ia ((struct in_ifaddr *)ifa)
	TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)
		if (ifa->ifa_addr->sa_family == AF_INET &&
		    (in.s_addr == ia->ia_broadaddr.sin_addr.s_addr ||
		     /*
		      * Check for old-style (host 0) broadcast, but
		      * taking into account that RFC 3021 obsoletes it.
		      */
		    (ia->ia_subnetmask != IN_RFC3021_MASK &&
		    t == ia->ia_subnet)) &&
		     /*
		      * Check for an all one subnetmask. These
		      * only exist when an interface gets a secondary
		      * address.
		      */
		    ia->ia_subnetmask != (u_long)0xffffffff)
			    return (1);
	return (0);
#undef ia
}

/*
 * On interface removal, clean up IPv4 data structures hung off of the ifnet.
 */
void
in_ifdetach(struct ifnet *ifp)
{

	in_pcbpurgeif0(&V_ripcbinfo, ifp);
	in_pcbpurgeif0(&V_udbinfo, ifp);
	in_purgemaddrs(ifp);
}

/*
 * Delete all IPv4 multicast address records, and associated link-layer
 * multicast address records, associated with ifp.
 * XXX It looks like domifdetach runs AFTER the link layer cleanup.
 * XXX This should not race with ifma_protospec being set during
 * a new allocation, if it does, we have bigger problems.
 */
static void
in_purgemaddrs(struct ifnet *ifp)
{
	LIST_HEAD(,in_multi) purgeinms;
	struct in_multi		*inm, *tinm;
	struct ifmultiaddr	*ifma;

	LIST_INIT(&purgeinms);
	IN_MULTI_LOCK();

	/*
	 * Extract list of in_multi associated with the detaching ifp
	 * which the PF_INET layer is about to release.
	 * We need to do this as IF_ADDR_LOCK() may be re-acquired
	 * by code further down.
	 */
	IF_ADDR_RLOCK(ifp);
	TAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_INET ||
		    ifma->ifma_protospec == NULL)
			continue;
#if 0
		KASSERT(ifma->ifma_protospec != NULL,
		    ("%s: ifma_protospec is NULL", __func__));
#endif
		inm = (struct in_multi *)ifma->ifma_protospec;
		LIST_INSERT_HEAD(&purgeinms, inm, inm_link);
	}
	IF_ADDR_RUNLOCK(ifp);

	LIST_FOREACH_SAFE(inm, &purgeinms, inm_link, tinm) {
		LIST_REMOVE(inm, inm_link);
		inm_release_locked(inm);
	}
	igmp_ifdetach(ifp);

	IN_MULTI_UNLOCK();
}

#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/netinet/if_ether.h>

struct in_llentry {
	struct llentry		base;
	struct bsd_sockaddr_in	l3_addr4;
};

/*
 * Deletes an address from the address table.
 * This function is called by the timer functions
 * such as arptimer() and nd6_llinfo_timer(), and
 * the caller does the locking.
 */
static void
in_lltable_free(struct lltable *llt, struct llentry *lle)
{
	LLE_WUNLOCK(lle);
	LLE_LOCK_DESTROY(lle);
	free(lle);
}

static struct llentry *
in_lltable_new(const struct bsd_sockaddr *l3addr, u_int flags)
{
	struct in_llentry *lle;

	lle = (in_llentry *)malloc(sizeof(struct in_llentry));
	if (lle == NULL)		/* NB: caller generates msg */
		return NULL;

	bzero(lle, sizeof(struct in_llentry));

	/*
	 * For IPv4 this will trigger "arpresolve" to generate
	 * an ARP request.
	 */
	lle->base.la_expire = time_uptime; /* mark expired */
	lle->l3_addr4 = *(const struct bsd_sockaddr_in *)l3addr;
	lle->base.lle_refcnt = 1;
	lle->base.lle_free = in_lltable_free;
	LLE_LOCK_INIT(&lle->base);
	callout_init_rw(&lle->base.la_timer, &lle->base.lle_lock,
	    CALLOUT_RETURNUNLOCKED);

	return (&lle->base);
}

#define IN_ARE_MASKED_ADDR_EQUAL(d, a, m)	(			\
	    (((ntohl((d)->sin_addr.s_addr) ^ (a)->sin_addr.s_addr) & (m)->sin_addr.s_addr)) == 0 )

static void
in_lltable_prefix_free(struct lltable *llt, const struct bsd_sockaddr *prefix,
    const struct bsd_sockaddr *mask, u_int flags)
{
	const struct bsd_sockaddr_in *pfx = (const struct bsd_sockaddr_in *)prefix;
	const struct bsd_sockaddr_in *msk = (const struct bsd_sockaddr_in *)mask;
	struct llentry *lle, *next;
	int i;
	size_t pkts_dropped;

	IF_AFDATA_WLOCK(llt->llt_ifp);
	for (i = 0; i < LLTBL_HASHTBL_SIZE; i++) {
		LIST_FOREACH_SAFE(lle, &llt->lle_head[i], lle_next, next) {
			/*
			 * (flags & LLE_STATIC) means deleting all entries
			 * including static ARP entries.
			 */
			if (IN_ARE_MASKED_ADDR_EQUAL(satosin(L3_ADDR(lle)),
			    pfx, msk) && ((flags & LLE_STATIC) ||
			    !(lle->la_flags & LLE_STATIC))) {
				LLE_WLOCK(lle);
				if (callout_stop(&lle->la_timer))
					LLE_REMREF(lle);
				pkts_dropped = llentry_free(lle);
				ARPSTAT_ADD(dropped, pkts_dropped);
			}
		}
	}
	IF_AFDATA_WUNLOCK(llt->llt_ifp);
}


static int
in_lltable_rtcheck(struct ifnet *ifp, u_int flags, const struct bsd_sockaddr *l3addr)
{
	struct rtentry *rt;

	KASSERT(l3addr->sa_family == AF_INET,
	    ("sin_family %d", l3addr->sa_family));

	/* XXX rtalloc1 should take a const param */
	rt = rtalloc1(__DECONST(struct bsd_sockaddr *, l3addr), 0, 0);

	if (rt == NULL)
		return (EINVAL);

	/*
	 * If the gateway for an existing host route matches the target L3
	 * address, which is a special route inserted by some implementation
	 * such as MANET, and the interface is of the correct type, then
	 * allow for ARP to proceed.
	 */
	if (rt->rt_flags & RTF_GATEWAY) {
		if (!(rt->rt_flags & RTF_HOST) || !rt->rt_ifp ||
		    rt->rt_ifp->if_type != IFT_ETHER ||
		    (rt->rt_ifp->if_flags & (IFF_NOARP | IFF_STATICARP)) != 0 ||
		    memcmp(rt->rt_gateway->sa_data, l3addr->sa_data,
		    sizeof(in_addr_t)) != 0) {
			RTFREE_LOCKED(rt);
			return (EINVAL);
		}
	}

	/*
	 * Make sure that at least the destination address is covered
	 * by the route. This is for handling the case where 2 or more
	 * interfaces have the same prefix. An incoming packet arrives
	 * on one interface and the corresponding outgoing packet leaves
	 * another interface.
	 */
	if (!(rt->rt_flags & RTF_HOST) && rt->rt_ifp != ifp) {
		const char *sa, *mask, *addr, *lim;
		int len;

		mask = (const char *)rt_mask(rt);
		/*
		 * Just being extra cautious to avoid some custom
		 * code getting into trouble.
		 */
		if (mask == NULL) {
			RTFREE_LOCKED(rt);
			return (EINVAL);
		}

		sa = (const char *)rt_key(rt);
		addr = (const char *)l3addr;
		len = ((const struct bsd_sockaddr_in *)l3addr)->sin_len;
		lim = addr + len;

		for ( ; addr < lim; sa++, mask++, addr++) {
			if ((*sa ^ *addr) & *mask) {
#ifdef DIAGNOSTIC
				bsd_log(LOG_INFO, "IPv4 address: \"%s\" is not on the network\n",
				    inet_ntoa(((const struct bsd_sockaddr_in *)l3addr)->sin_addr));
#endif
				RTFREE_LOCKED(rt);
				return (EINVAL);
			}
		}
	}

	RTFREE_LOCKED(rt);
	return (0);
}

static bool
in_lltable_lookup_fast(struct lltable *llt, u_int flags, const struct bsd_sockaddr *l3addr,
		arp_cache::mac_address& mac, u16& la_flags)
{
	trace_in_lltable_lookup_fast(llt, flags);
	auto* sin = (const struct bsd_sockaddr_in *)l3addr;
	return arp_cache_lookup(sin->sin_addr, mac, la_flags);
}

/*
 * Return NULL if not found or marked for deletion.
 * If found return lle read locked.
 */
static struct llentry *
in_lltable_lookup(struct lltable *llt, u_int flags, const struct bsd_sockaddr *l3addr)
{
	const struct bsd_sockaddr_in *sin = (const struct bsd_sockaddr_in *)l3addr;
	struct ifnet *ifp = llt->llt_ifp;
	struct llentry *lle;
	struct llentries *lleh;
	u_int hashkey;

	trace_in_lltable_lookup(llt, flags);

	IF_AFDATA_LOCK_ASSERT(ifp);
	KASSERT(l3addr->sa_family == AF_INET,
	    ("sin_family %d", l3addr->sa_family));

	hashkey = sin->sin_addr.s_addr;
	lleh = &llt->lle_head[LLATBL_HASH(hashkey, LLTBL_HASHMASK)];
	LIST_FOREACH(lle, lleh, lle_next) {
		struct bsd_sockaddr_in *sa2 = satosin(L3_ADDR(lle));
		if (lle->la_flags & LLE_DELETED)
			continue;
		if (sa2->sin_addr.s_addr == sin->sin_addr.s_addr)
			break;
	}
	if (lle == NULL) {
#ifdef DIAGNOSTIC
		if (flags & LLE_DELETE)
			bsd_log(LOG_INFO, "interface address is missing from cache = %p  in delete\n", lle);
#endif
		if (!(flags & LLE_CREATE))
			return (NULL);
		/*
		 * A route that covers the given address must have
		 * been installed 1st because we are doing a resolution,
		 * verify this.
		 */
		if (!(flags & LLE_IFADDR) &&
		    in_lltable_rtcheck(ifp, flags, l3addr) != 0)
			goto done;

		lle = in_lltable_new(l3addr, flags);
		if (lle == NULL) {
			bsd_log(LOG_INFO, "lla_lookup: new lle malloc failed\n");
			goto done;
		}
		lle->la_flags = flags & ~LLE_CREATE;
		if ((flags & (LLE_CREATE | LLE_IFADDR)) == (LLE_CREATE | LLE_IFADDR)) {
			bcopy(IF_LLADDR(ifp), &lle->ll_addr, ifp->if_addrlen);
			lle->la_flags |= (LLE_VALID | LLE_STATIC);
		}

		lle->lle_tbl  = llt;
		lle->lle_head = lleh;
		lle->la_flags |= LLE_LINKED;
		LIST_INSERT_HEAD(lleh, lle, lle_next);
		arp_cache_add(lle);
	} else if (flags & LLE_DELETE) {
		if (!(lle->la_flags & LLE_IFADDR) || (flags & LLE_IFADDR)) {
			LLE_WLOCK(lle);
			lle->la_flags |= LLE_DELETED;
			EVENTHANDLER_INVOKE(arp_update_event, lle);
			LLE_WUNLOCK(lle);
#ifdef DIAGNOSTIC
			bsd_log(LOG_INFO, "bsd_ifaddr cache = %p  is deleted\n", lle);
#endif
		}
		arp_cache_remove(lle);
		lle = (llentry *)(void *)-1;
	}
	if (LLE_IS_VALID(lle)) {
		if (flags & LLE_EXCLUSIVE)
			LLE_WLOCK(lle);
		else
			LLE_RLOCK(lle);
	}
done:
	return (lle);
}

static int
in_lltable_dump(struct lltable *llt, struct sysctl_req *wr)
{
#define	SIN(lle)	((struct bsd_sockaddr_in *) L3_ADDR(lle))
	struct ifnet *ifp = llt->llt_ifp;
	struct llentry *lle;
	/* XXX stack use */
	struct {
		struct rt_msghdr	rtm;
		struct bsd_sockaddr_inarp	sin;
		struct bsd_sockaddr_dl	sdl;
	} arpc;
	int error, i;

	LLTABLE_LOCK_ASSERT();

	error = 0;
	for (i = 0; i < LLTBL_HASHTBL_SIZE; i++) {
		LIST_FOREACH(lle, &llt->lle_head[i], lle_next) {
			struct bsd_sockaddr_dl *sdl;

			/* skip deleted entries */
			if ((lle->la_flags & LLE_DELETED) == LLE_DELETED)
				continue;
			/*
			 * produce a msg made of:
			 *  struct rt_msghdr;
			 *  struct bsd_sockaddr_inarp; (IPv4)
			 *  struct bsd_sockaddr_dl;
			 */
			bzero(&arpc, sizeof(arpc));
			arpc.rtm.rtm_msglen = sizeof(arpc);
			arpc.rtm.rtm_version = RTM_VERSION;
			arpc.rtm.rtm_type = RTM_GET;
			arpc.rtm.rtm_flags = RTF_UP;
			arpc.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY;
			arpc.sin.sin_family = AF_INET;
			arpc.sin.sin_len = sizeof(arpc.sin);
			arpc.sin.sin_addr.s_addr = SIN(lle)->sin_addr.s_addr;

			/* publish */
			if (lle->la_flags & LLE_PUB) {
				arpc.rtm.rtm_flags |= RTF_ANNOUNCE;
				/* proxy only */
				if (lle->la_flags & LLE_PROXY)
					arpc.sin.sin_other = SIN_PROXY;
			}

			sdl = &arpc.sdl;
			sdl->sdl_family = AF_LINK;
			sdl->sdl_len = sizeof(*sdl);
			sdl->sdl_index = ifp->if_index;
			sdl->sdl_type = ifp->if_type;
			if ((lle->la_flags & LLE_VALID) == LLE_VALID) {
				sdl->sdl_alen = ifp->if_addrlen;
				bcopy(&lle->ll_addr, LLADDR(sdl), ifp->if_addrlen);
			} else {
				sdl->sdl_alen = 0;
				bzero(LLADDR(sdl), ifp->if_addrlen);
			}

			arpc.rtm.rtm_rmx.rmx_expire =
			    lle->la_flags & LLE_STATIC ? 0 : lle->la_expire;
			arpc.rtm.rtm_flags |= (RTF_HOST | RTF_LLDATA);
			if (lle->la_flags & LLE_STATIC)
				arpc.rtm.rtm_flags |= RTF_STATIC;
			arpc.rtm.rtm_index = ifp->if_index;
			/* FIXME OSv: output as a response to sysreq */
//			error = SYSCTL_OUT(wr, &arpc, sizeof(arpc));
//			if (error)
//				break;
		}
	}
	return error;
#undef SIN
}

void *
in_domifattach(struct ifnet *ifp)
{
	struct in_ifinfo *ii;
	struct lltable *llt;

	ii = (in_ifinfo *)malloc(sizeof(struct in_ifinfo));
	bzero(ii, sizeof(struct in_ifinfo));

	llt = lltable_init(ifp, AF_INET);
	if (llt != NULL) {
		llt->llt_prefix_free = in_lltable_prefix_free;
		llt->llt_lookup = in_lltable_lookup;
		llt->llt_lookup_fast = in_lltable_lookup_fast;
		llt->llt_dump = in_lltable_dump;
	}
	ii->ii_llt = llt;

	ii->ii_igmp = igmp_domifattach(ifp);

	return ii;
}

void
in_domifdetach(struct ifnet *ifp, void *aux)
{
	struct in_ifinfo *ii = (struct in_ifinfo *)aux;

	igmp_domifdetach(ifp);
	lltable_free(ii->ii_llt);
	free(ii);
}
