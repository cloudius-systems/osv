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

    extern  struct domain inetdomain;
}


void net_init(void)
{
    debug("Initializing network stack...");

    /* Random */
    struct timeval tv;
    bsd_srandom(tv.tv_sec ^ tv.tv_usec);

    arc4_init();
    eventhandler_init(NULL);

    /* MBUF */
    tunable_mbinit(NULL);
    mbuf_init(NULL);

    netisr_init(NULL);
    vnet_lltable_init();
    arp_init();
    ether_init(NULL);
    if_init(NULL);
    vnet_if_init(NULL);

    /* Routing */
    route_init();
    vnet_route_init();

    rts_init();

    vnet_pfil_init();

    ip_initid();
    ipport_tick_init(NULL);


    /* Initialize Domains */
    domaininit(NULL);
    OSV_DOMAIN_SET(inet);

    /* IGMP */
    igmp_init();
    vnet_igmp_init();

    /* Loopback */
    vnet_loif_init();

    /*
     * Let all domains know about this interface...
     * (There are non configured at this moment)
     */
    if_attachdomain(NULL);

    debug("Done!");
}
