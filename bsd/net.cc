/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>

#include <bsd/porting/netport.h>
#include <bsd/porting/networking.hh>
#include <bsd/porting/route.h>

#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/domain.h>
#include <bsd/sys/net/netisr.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/net/pfil.h>
#include <bsd/sys/netinet/igmp.h>
#include <bsd/sys/netinet/if_ether.h>
#include <bsd/sys/netinet/in_pcb.h>
#include <bsd/sys/netinet/cc.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/compat/linux/linux_netlink.h>
#ifdef INET6
#include <bsd/sys/netinet/icmp6.h>
#include <bsd/sys/netinet6/in6.h>
#include <bsd/sys/netinet6/mld6.h>
#endif

/* Generation of ip ids */
void ip_initid(void);

extern "C" {
    /* AF_INET */
    extern  struct domain inetdomain;
#ifdef INET6
    /* AF_INET6 */
    extern  struct domain inet6domain;
#endif
    /* AF_ROUTE */
    extern  struct domain routedomain;
    /* AF_NETLINK */
    extern  struct domain netlinkdomain;
}

void net_init(void)
{
    debug("net: initializing");

    ip_initid();
    tunable_mbinit(NULL);
    init_maxsockets(NULL);
    mbuf_init(NULL);
    netisr_init(NULL);
    if_init(NULL);
    vnet_if_init(NULL);
    ether_init(NULL);
    vnet_lltable_init();
    igmp_init();
    vnet_igmp_init();
    vnet_pfil_init();
    domaininit(NULL);
    OSV_DOMAIN_SET(inet);
#ifdef INET6
    OSV_DOMAIN_SET(inet6);
#endif
    OSV_DOMAIN_SET(route);
    OSV_DOMAIN_SET(netlink);
    rts_init();
    route_init();
    vnet_route_init();
    netlink_init();
#ifdef INET6
    ip6_init2(NULL);
    mld_init(NULL);
    vnet_mld_init(NULL);
#endif
    ipport_tick_init(NULL);
    arp_init();
    domainfinalize(NULL);
    cc_init();
    if_attachdomain(NULL);
    vnet_loif_init();

    /* Adding IPv4 address before starting the loopback interface
     * cause the interface to be brought up without IPv6 support.
     */

    /* Start the loopback device */
    osv::ifup("lo0");
    osv::start_if("lo0", "127.0.0.1", "255.0.0.0");

    debug(" - done\n");
}
