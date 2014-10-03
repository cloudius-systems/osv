#ifndef _ZCOPY_HH
#define _ZCOPY_HH

#include <osv/zcopy.h>

struct ztx_handle {
    ztx_handle() : zh_remained(0) {};
    std::atomic<size_t> zh_remained;
};

#endif
