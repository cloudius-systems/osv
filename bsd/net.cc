#include "debug.hh"

extern "C" {
    #include <sys/eventhandler.h>
    #include <sys/mbuf.h>
    #include <machine/param.h>
}

extern void mbuf_init(void *dummy);

void test_mbuf(void)
{
    debug("A");
    eventhandler_init(NULL);
    mbuf_init(NULL);
    debug("B");
}
