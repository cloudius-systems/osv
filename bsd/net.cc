#include "debug.hh"

extern "C" {
    #include <bsd/sys/sys/mbuf.h>
    #include <bsd/sys/amd64/include/param.h>
}

extern void mbuf_init(void *dummy);

void test_mbuf(void)
{
    debug("A");
    mbuf_init(NULL);
    debug("B");
}
