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

#include "libc/stdio/stdio.h"           /* for sscanf() */

#include <bsd/porting/netport.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/netinet/if_ether.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>

/*
 * Routing message structure -
 *
 * The addresses (sockaddrs) goes into m_space
 * see usr.sbin/arp.c and sbin/route/route.c in FreeBSD 9
 */
struct rt_msg {
    struct  rt_msghdr m_rtm;
    char    m_space[512];
};

/*
 * Convert an ASCII representation of an ethernet address to binary form.
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

static struct sockaddr_inarp getaddr_inarp(const char* host)
{
    static struct sockaddr_inarp reply;

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
    struct sockaddr_inarp dst; /* what are we looking for */
    struct sockaddr_dl sdl_m;
    struct ether_addr *ea;

    /*
     * Init
     */

    m = m_getcl(M_DONTWAIT | M_ZERO, MT_DATA, M_PKTHDR);
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

#define CP_ADDR(w, sa) \
    l = SA_SIZE_ALWAYS(&(sa)); bcopy(&(sa), cp, l); cp += l;\

    CP_ADDR(RTA_DST, dst);
    CP_ADDR(RTA_GATEWAY, sdl_m);

#undef CP_ADDR

    m_rtmsg->m_rtm.rtm_msglen = cp - (char *)m_rtmsg;
    m->m_pkthdr.len = m->m_len = m_rtmsg->m_rtm.rtm_msglen;

    return (m);
}

/* Compose a routing message to be sent on socket */
static struct mbuf*  osv_route_rtmsg(int cmd, const char* destination,
    const char* gateway, const char* netmask, int flags)
{
    static int msg_seq = 0;
    struct mbuf* m;
    struct rt_msg* m_rtmsg;
    char *cp;
    int l = 0;
    int rtm_addrs;

    /* IPv4: Addresses */
    struct sockaddr_in dst;
    struct sockaddr_in gw;
    struct sockaddr_in mask;

    /*
     * Init
     */

    m = m_getcl(M_DONTWAIT | M_ZERO, MT_DATA, M_PKTHDR);
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
    bzero(&mask, sizeof(mask));

    dst.sin_family = AF_INET;
    dst.sin_len = sizeof(struct sockaddr_in);
    inet_aton(destination, &dst.sin_addr);

    gw.sin_family = AF_INET;
    gw.sin_len = sizeof(struct sockaddr_in);
    inet_aton(gateway, &gw.sin_addr);

    if (netmask) {
        mask.sin_family = AF_INET;
        mask.sin_len = sizeof(struct sockaddr_in);
        inet_aton(netmask, &mask.sin_addr);
    }

    /*
     * Compose routing message
     */

    m_rtmsg->m_rtm.rtm_type = cmd;
    m_rtmsg->m_rtm.rtm_flags = flags;
    m_rtmsg->m_rtm.rtm_version = RTM_VERSION;
    m_rtmsg->m_rtm.rtm_seq = ++msg_seq;
    m_rtmsg->m_rtm.rtm_addrs = rtm_addrs;
    /*
        FIXME: OSv - support metrics such as MTU, weight...
        m_rtmsg->m_rtm.rtm_rmx = rt_metrics;
        m_rtmsg->m_rtm.rtm_inits = rtm_inits;
    */

#define CP_ADDR(w, sa) \
    l = SA_SIZE_ALWAYS(&(sa)); bcopy(&(sa), cp, l); cp += l;\

    CP_ADDR(RTA_DST, dst);
    CP_ADDR(RTA_GATEWAY, gw);
    if (netmask) {
        CP_ADDR(RTA_NETMASK, mask);
    }

#undef CP_ADDR

    m_rtmsg->m_rtm.rtm_msglen = cp - (char *)m_rtmsg;
    m->m_pkthdr.len = m->m_len = m_rtmsg->m_rtm.rtm_msglen;

    return (m);
}

void osv_route_add_host(const char* destination,
    const char* gateway)
{
    /* Create socket */
    struct socket* s;
    struct mbuf *m;

    m = osv_route_rtmsg(RTM_ADD, destination, gateway, NULL,
        (RTF_STATIC | RTF_UP | RTF_HOST) );

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
    struct ifaddr *ifa, *next;

    ifp = ifunit_ref(if_name);

    TAILQ_FOREACH_SAFE(ifa, &ifp->if_addrhead, ifa_link, next) {
        if (ifa->ifa_addr->sa_family == AF_LINK)
                break;
    }

    return ifa->ifa_addr->sa_data;
}
