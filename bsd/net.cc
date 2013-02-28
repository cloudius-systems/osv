#include "debug.hh"

extern "C" {
    #include <sys/time.h>

    #include <bsd/porting/netport.h>
    #include <bsd/sys/sys/libkern.h>
    #include <bsd/sys/sys/eventhandler.h>
    #include <bsd/sys/sys/mbuf.h>
    #include <bsd/sys/sys/domain.h>
    #include <bsd/sys/net/netisr.h>
    #include <bsd/sys/net/if.h>
    #include <bsd/sys/net/if_llatbl.h>
    #include <bsd/sys/net/pfil.h>
    #include <bsd/sys/netinet/igmp.h>
    #include <bsd/sys/netinet/if_ether.h>
    #include <bsd/sys/netinet/in_pcb.h>
    #include <bsd/sys/net/ethernet.h>
    #include <bsd/sys/net/route.h>
    #include <bsd/machine/param.h>

    /* Generation of ip ids */
    void ip_initid(void);

    /* AF_INET */
    extern  struct domain inetdomain;
    /* AF_ROUTE */
    extern  struct domain routedomain;
}


void net_init(void)
{
    debug("Initializing network stack...");

    /* Random */
    struct timeval tv;
    bsd_srandom(tv.tv_sec ^ tv.tv_usec);
    ip_initid();

    tunable_mbinit(NULL);
    // init_maxsockets(NULL);
    arc4_init();
    eventhandler_init(NULL);
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
    OSV_DOMAIN_SET(route);
    rts_init();
    route_init();
    vnet_route_init();
    ipport_tick_init(NULL);
    arp_init();
    domainfinalize(NULL);
    if_attachdomain(NULL);
    vnet_loif_init();

    debug("Done!");
}
