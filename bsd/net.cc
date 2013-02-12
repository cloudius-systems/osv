#include "debug.hh"

extern "C" {
    #include <bsd/sys/sys/eventhandler.h>
    #include <bsd/sys/sys/mbuf.h>
    #include <bsd/machine/param.h>
}

extern void mbuf_init(void *dummy);

void test_mbuf(void)
{
    debug("A");
    eventhandler_init(NULL);
    mbuf_init(NULL);
    debug("B");
}
