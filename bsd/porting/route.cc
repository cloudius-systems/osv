/*
 * Copyright (c) 1983, 1989, 1991, 1993
 *  The Regents of the University of California.  All rights reserved.
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
 */

#include <stdio.h>           /* for sscanf() */

#include <bsd/porting/netport.h>
#include <bsd/porting/route.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/netinet/if_ether.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <bsd/sys/sys/sysctl.h>
#ifdef INET6
#include <bsd/sys/netinet6/in6.h>
#include <bsd/sys/netinet6/in6_var.h>
#endif

int sysctl_rtsock(SYSCTL_HANDLER_ARGS) ;

enum class gw_type: u8 {
    inet    = 0, // Gateway is a internet address
    link    = 1  // Gateway is a link address
};

/*
 * Routing message structure -
 *
 * The addresses (bsd_sockaddrs) goes into m_space
 * see usr.sbin/arp.c and sbin/route/route.c in FreeBSD 9
 */
struct rt_msg {
    struct  rt_msghdr m_rtm;
    char    m_space[512];
};

/*
 * Convert an ASCII representation of an Ethernet address to binary form.
 */
static struct ether_addr *
ether_aton_r(const char *a, struct ether_addr *e)
{
    int i;
    unsigned int o0, o1, o2, o3, o4, o5;

    i = sscanf(a, "%x:%x:%x:%x:%x:%x", &o0, &o1, &o2, &o3, &o4, &o5);
    if (i != 6)
        return (NULL);
    e->octet[0]=o0;
    e->octet[1]=o1;
    e->octet[2]=o2;
    e->octet[3]=o3;
    e->octet[4]=o4;
    e->octet[5]=o5;
    return (e);
}

static struct ether_addr *
ether_aton(const char *a)
{
    static struct ether_addr e;

    return (ether_aton_r(a, &e));
}

static struct bsd_sockaddr_inarp getaddr_inarp(const char* host)
{
    static struct bsd_sockaddr_inarp reply;

    bzero(&reply, sizeof(reply));
    reply.sin_len = sizeof(reply);
    reply.sin_family = AF_INET;
    inet_aton(host, &reply.sin_addr);
    return (reply);
}

/* Compose a routing message to be sent on socket */
static struct mbuf*  osv_route_arp_rtmsg(int if_idx, int cmd, const char* ip,
    const char* macaddr)
{
    static int msg_seq = 0;
    struct mbuf* m;
    struct rt_msg* m_rtmsg;
    char *cp;
    int l = 0;

    /* ARP Addresses */
    struct bsd_sockaddr_inarp dst; /* what are we looking for */
    struct bsd_sockaddr_dl sdl_m;
    struct ether_addr *ea;

    /*
     * Init
     */

    m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
    m_rtmsg = mtod(m, struct rt_msg*);
    cp = m_rtmsg->m_space;

    /*
     * Setup Addresses
     */

    /* MAC Address */
    bzero(&sdl_m, sizeof(sdl_m));
    sdl_m.sdl_len = sizeof(sdl_m);
    sdl_m.sdl_family = AF_LINK;
    sdl_m.sdl_alen = ETHER_ADDR_LEN;
    sdl_m.sdl_type = IFT_ETHER;
    sdl_m.sdl_index = if_idx;

    ea = (struct ether_addr *)LLADDR(&sdl_m);
    *ea = *ether_aton(macaddr);

    /* IP */
    dst = getaddr_inarp(ip);

    /*
     * Compose routing message
     */

    m_rtmsg->m_rtm.rtm_type = cmd;
    m_rtmsg->m_rtm.rtm_flags = (RTF_HOST | RTF_STATIC | RTF_LLDATA);
    m_rtmsg->m_rtm.rtm_version = RTM_VERSION;
    m_rtmsg->m_rtm.rtm_rmx.rmx_expire = 0;
    m_rtmsg->m_rtm.rtm_inits = RTV_EXPIRE;
    m_rtmsg->m_rtm.rtm_seq = ++msg_seq;
    m_rtmsg->m_rtm.rtm_addrs = (RTA_GATEWAY | RTA_DST);

    // Note that (((struct bsd_sockaddr *)(sa))->sa_len) is the real
    // length of the address structure, but in the routing socket we're
    // supposed to round the length to multiples of 8 bytes (long).
    // We still can't *copy* this much from the input address structure,
    // because the compiler (starting from gcc 8) would warn about it.
#define CP_ADDR(w, sa) \
    l = SA_SIZE_ALWAYS(&(sa)); bcopy(&(sa), cp, (((struct bsd_sockaddr *)(&(sa)))->sa_len)); cp += l;\

    CP_ADDR(RTA_DST, dst);
    CP_ADDR(RTA_GATEWAY, sdl_m);

    m_rtmsg->m_rtm.rtm_msglen = cp - (char *)m_rtmsg;
    m->M_dat.MH.MH_pkthdr.len = m->m_hdr.mh_len = m_rtmsg->m_rtm.rtm_msglen;

    return (m);
}

static int osv_sockaddr_from_string(struct bsd_sockaddr_storage *addr, const char *str)
{
    struct bsd_sockaddr_in *sa4 = (struct bsd_sockaddr_in*)addr;
    if (inet_pton(AF_INET, str, (void*)&sa4->sin_addr)) { 
        sa4->sin_len = sizeof(*sa4);
        sa4->sin_family = AF_INET;
        sa4->sin_port = 0;
        return 1;
    }
#ifdef INET6
    struct bsd_sockaddr_in6 *sa6 = (struct bsd_sockaddr_in6*)addr;
    if (inet_pton(AF_INET6, str, (void*)&sa6->sin6_addr)) {
        sa6->sin6_len = sizeof(*sa6);
        sa6->sin6_family = AF_INET6;
        sa6->sin6_port = 0;
        sa6->sin6_flowinfo = 0;
        sa6->sin6_scope_id = 0;
        return 1;
    }
#endif
    return 0;
}

static int osv_sockaddr_from_prefix_len(int af, struct bsd_sockaddr_storage *addr, int prefix_len)
{
    switch(af){
    case AF_INET:
        {
            struct bsd_sockaddr_in *sa4 = (struct bsd_sockaddr_in *)addr;
            sa4->sin_len = sizeof(*sa4);
            sa4->sin_family = AF_INET;
            sa4->sin_port = 0;
            in_prefixlen2mask(&sa4->sin_addr, prefix_len);
        }
        return 1;
#ifdef INET6
    case AF_INET6:
        {
            struct bsd_sockaddr_in6 *sa6 = (struct bsd_sockaddr_in6 *)addr;
            sa6->sin6_len = sizeof(*sa6);
            sa6->sin6_family = AF_INET6;
            sa6->sin6_port = 0;
            sa6->sin6_flowinfo = 0;
            sa6->sin6_scope_id = 0;
            in6_prefixlen2mask(&sa6->sin6_addr, prefix_len);
        }
        return 1;
#endif
    default:
        return 0;
    }
}

/* Compose a routing message to be sent on socket */
static struct mbuf*  osv_route_rtmsg(int cmd, const char* destination,
    const char* gateway, const char* netmask, int flags, gw_type type)
{
    static int msg_seq = 0;
    struct mbuf* m;
    struct rt_msg* m_rtmsg;
    char *cp;
    int l = 0;
    int rtm_addrs;
    struct bsd_ifaddr *ifa;
    bool is_link = type == gw_type::link;

    /* IP: Addresses */
    struct bsd_sockaddr_storage dst;
    struct bsd_sockaddr_storage gw;
    struct bsd_sockaddr_storage mask;

    /* Link: Address*/
    struct bsd_sockaddr_dl sdl;

    /*
     * Init
     */

    m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
    m_rtmsg = mtod(m, struct rt_msg*);
    cp = m_rtmsg->m_space;
    rtm_addrs = (RTA_DST | RTA_GATEWAY);

    if (netmask) {
        rtm_addrs |= RTA_NETMASK;
    }

    /*
     * Setup Addresses
     */

    bzero(&dst, sizeof(dst));
    bzero(&gw, sizeof(gw));
    bzero(&sdl, sizeof(sdl));
    bzero(&mask, sizeof(mask));

    osv_sockaddr_from_string(&dst, destination);

    if (is_link) {
        /* Get ifindex from name */
        auto ifp = ifunit_ref(gateway);
        if (ifp == nullptr) {
            abort();
        }
        sdl.sdl_len = sizeof(sdl);
        sdl.sdl_family = AF_LINK;
        sdl.sdl_alen = ETHER_ADDR_LEN;
        sdl.sdl_type = IFT_ETHER;
        sdl.sdl_index = ifp->if_index;
        auto ea = (struct ether_addr *)LLADDR(&sdl);
        memcpy(ea, IF_LLADDR(ifp), ETHER_ADDR_LEN);
        if_rele(ifp);
    } else {
        osv_sockaddr_from_string(&gw, gateway);
    }

    if (netmask) {
        if (osv_sockaddr_from_string(&mask, netmask) == 0) {
            // Try parsing it as a prefix length
            char *p_end = NULL;
            long prefix_len = strtol(netmask, &p_end, 0);
            if (p_end == netmask) {
                 // Bad netmask string.  Probably safer to treat it as a host route.
                 prefix_len = (((struct bsd_sockaddr *)&dst)->sa_family == AF_INET6) ? 128 : 32;
            }
            osv_sockaddr_from_prefix_len(((struct bsd_sockaddr *)&dst)->sa_family, &mask, prefix_len);
        }
    }

    /*
     * Compose routing message
     */

    m_rtmsg->m_rtm.rtm_type = cmd;
    m_rtmsg->m_rtm.rtm_flags = flags;
    m_rtmsg->m_rtm.rtm_version = RTM_VERSION;
    m_rtmsg->m_rtm.rtm_seq = ++msg_seq;
    m_rtmsg->m_rtm.rtm_addrs = rtm_addrs;

    if ((flags & RTF_GATEWAY) && !is_link) {
        ifa = ifa_ifwithnet((struct bsd_sockaddr *)&gw, 1);
        if (ifa) {
            m_rtmsg->m_rtm.rtm_rmx.rmx_mtu = ifa->ifa_ifp->if_mtu;
        }
    }
    /*
        FIXME: OSv - support other metrics such as weight, etc...
        m_rtmsg->m_rtm.rtm_rmx = rt_metrics;
        m_rtmsg->m_rtm.rtm_inits = rtm_inits;
    */

    CP_ADDR(RTA_DST, dst);
    if (is_link) {
        CP_ADDR(RTA_GATEWAY, sdl);
    } else {
        CP_ADDR(RTA_GATEWAY, gw);
    }
    if (netmask) {
        CP_ADDR(RTA_NETMASK, mask);
    }

    m_rtmsg->m_rtm.rtm_msglen = cp - (char *)m_rtmsg;
    m->M_dat.MH.MH_pkthdr.len = m->m_hdr.mh_len = m_rtmsg->m_rtm.rtm_msglen;

    return (m);
}

void osv_route_add_interface(const char* destination, const char* netmask, const char* interface)
{
    /* Create socket */
    struct socket* s;
    struct mbuf *m;
    int flags = 0;

    if (!netmask) {
        flags = RTF_HOST;
    }

    m = osv_route_rtmsg(RTM_ADD, destination, interface, netmask,
        (RTF_STATIC | RTF_UP | flags), gw_type::link);

    /* Send routing message */
    socreate(PF_ROUTE, &s, SOCK_RAW, 0, NULL, NULL);
    sosend(s, 0, 0, m, 0, 0, NULL);
    soclose(s);
}

void osv_route_add_host(const char* destination,
    const char* gateway)
{
    /* Create socket */
    struct socket* s;
    struct mbuf *m;

    m = osv_route_rtmsg(RTM_ADD, destination, gateway, NULL,
        (RTF_STATIC | RTF_UP | RTF_HOST), gw_type::inet);

    /* Send routing message */
    socreate(PF_ROUTE, &s, SOCK_RAW, 0, NULL, NULL);
    sosend(s, 0, 0, m, 0, 0, NULL);
    soclose(s);
}

void osv_route_add_network(const char* destination, const char* netmask,
    const char* gateway)
{
    /* Create socket */
    struct socket* s;
    struct mbuf *m;

    m = osv_route_rtmsg(RTM_ADD, destination, gateway, netmask,
        (RTF_STATIC | RTF_UP | RTF_GATEWAY ), gw_type::inet);

    /* Send routing message */
    socreate(PF_ROUTE, &s, SOCK_RAW, 0, NULL, NULL);
    sosend(s, 0, 0, m, 0, 0, NULL);
    soclose(s);
}

void osv_route_arp_add(const char* if_name, const char* ip,
    const char* macaddr)
{
    struct socket* s;
    struct ifnet* ifp;
    struct mbuf *m;
    int if_idx;

    /* Get ifindex from name */
    ifp = ifunit_ref(if_name);
    if (ifp == NULL) {
        return;
    }
    if_idx = ifp->if_index;
    if_rele(ifp);

    /* Compose mbuf packet containing routing message */
    m = osv_route_arp_rtmsg(if_idx, RTM_ADD, ip, macaddr);

    /* Send routing message */
    socreate(PF_ROUTE, &s, SOCK_RAW, 0, NULL, NULL);
    sosend(s, 0, 0, m, 0, 0, NULL);
    soclose(s);
}


const char* osv_get_if_mac_addr(const char* if_name)
{
    struct ifnet* ifp;
    struct bsd_ifaddr *ifa, *next;

    /* FIXME: where's the unref? */
    ifp = ifunit_ref(if_name);

    TAILQ_FOREACH_SAFE(ifa, &ifp->if_addrhead, ifa_link, next) {
        if (ifa->ifa_addr->sa_family == AF_LINK)
                break;
    }

    return ifa->ifa_addr->sa_data;
}

/*
 * Transfer functions to/from kernel space.
 * XXX: rather untested at this point
 */
static int
sysctl_old_kernel(struct sysctl_req *req, const void *p, size_t l)
{
	size_t i = 0;

	if (req->oldptr) {
		i = l;
		if (req->oldlen <= req->oldidx)
			i = 0;
		else if (i > req->oldlen - req->oldidx)
				i = req->oldlen - req->oldidx;
		if (i > 0)
			bcopy(p, (char *)req->oldptr + req->oldidx, i);
	}
	req->oldidx += l;
	if (req->oldptr && i != l)
		return (ENOMEM);
	return (0);
}

static int
sysctl_new_kernel(struct sysctl_req *req, void *p, size_t l)
{
	if (!req->newptr)
		return (0);
	if (req->newlen - req->newidx < l)
		return (EINVAL);
	bcopy((char *)req->newptr + req->newidx, p, l);
	req->newidx += l;
	return (0);
}
/*
 * Our limited version of sysctl. The arguments are the same as with the
 * standard sysctl(), however only  routing sysctls are currently supported
 * (and not necessarily all of thym). This version assumes working in kernel 
 * space.
 *
 * \param name      (input)  A mib (array) containing numbers defining the
 *                           operation and arguments. The first three numbers
 *                           are ignored.
 * \param namelen   (input)  Number of elements in name
 * \param old_buf   (output) 
 * \param oldlenp   (inout)  Available length on input actual length on
 *                           output    
 * \param new_buf   (input)
 * \param newlen    (input)
 *
 * \returns error code (see errno.h)
 */
int
osv_sysctl(int *name, u_int namelen, void *old_buf,
    size_t *oldlenp, void *new_buf, size_t newlen)
{
	int error = 0;
	struct sysctl_req req;
	bzero(&req, sizeof req);

	req.td    = NULL;
	req.flags = 0;

	if (oldlenp) {
		req.oldlen = *oldlenp;
	}
	req.validlen = req.oldlen;

	if (old_buf) {
		req.oldptr= old_buf;
	}

	if (new_buf != NULL) {
		req.newlen = newlen;
		req.newptr = new_buf;
	}

	req.oldfunc = sysctl_old_kernel;
	req.newfunc = sysctl_new_kernel;
	req.lock    = REQ_UNWIRED;

	error = sysctl_rtsock(NULL, name + 2, namelen - 2, &req);
	if (oldlenp) {
		*oldlenp = req.oldidx;
	}

	return (error);
}
