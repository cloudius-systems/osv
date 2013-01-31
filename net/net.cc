#include "debug.hh"

extern "C" {
    #include "mbuf.h"
    #include "param.h"
}

extern void mbuf_init(void *dummy);

void test_mbuf(void)
{
    debug("A");
    mbuf_init(NULL);
    debug("B");
}
