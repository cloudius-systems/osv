#include "debug.hh"

extern "C" {
    #include <bsd/sys/sys/eventhandler.h>
    #include <bsd/sys/sys/mbuf.h>
    #include <bsd/sys/net/netisr.h>
    #include <bsd/machine/param.h>
}

void net_init(void)
{
    debug("Initializing network stack...");
    eventhandler_init(NULL);
    mbuf_init(NULL);
    netisr_init(NULL);
    debug("Done!");
}
