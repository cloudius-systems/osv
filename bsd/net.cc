#include "debug.hh"

extern "C" {
    #include <bsd/porting/netport.h>
    #include <bsd/sys/sys/eventhandler.h>
    #include <bsd/sys/sys/mbuf.h>
    #include <bsd/sys/net/netisr.h>
    #include <bsd/sys/net/if.h>
    #include <bsd/sys/netinet/if_ether.h>
    #include <bsd/sys/net/ethernet.h>
    #include <bsd/sys/net/route.h>
    #include <bsd/machine/param.h>
}

void net_init(void)
{
    debug("Initializing network stack...");
    eventhandler_init(NULL);
    mbuf_init(NULL);
    netisr_init(NULL);
    arp_init();
    ether_init(NULL);
    if_init(NULL);
    vnet_if_init(NULL);
    domaininit(NULL);
    route_init();
    vnet_route_init();
    debug("Done!");
}
