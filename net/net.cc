extern "C" {
    #include "mbuf.h"
    #include "param.h"
}

extern void mbuf_init(void *dummy);

void test_mbuf(void)
{
    mbuf_init(NULL);
}
