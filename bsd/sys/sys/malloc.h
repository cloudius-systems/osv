#ifndef _BSD_MALLOC_H
#define _BSD_MALLOC_H
#include <bsd/porting/mmu.h>
#include <bsd/porting/netport.h>
// just our malloc impl.
#include <malloc.h>
#include "param.h" // BSD malloc includes this, so some files do not.
                   // Also differentiate from include/api/sys/param.h
#include "priority.h"

// But BSD malloc has extra parameters, that we will ignore. The third parameter though 
// is important, since it can be asking us to zero memory. That one we honor
static inline void *__bsd_malloc(size_t size, int flags)
{
    void *ptr = malloc(size);
    if (!ptr)
        return ptr;
    if (flags & M_ZERO)
        memset(ptr, 0, size);
    return ptr;
}

#ifdef __FBSDID

#ifndef __cplusplus

#define malloc(x, y, z) __bsd_malloc(x, z)
#define free(x, y) free(x)
#define strdup(x, y) strdup(x)

#else

enum bsd_malloc_arena {
    M_WHATEVER,
    M_DEVBUF,
    M_XENSTORE,
    M_XENBLOCKFRONT,
    M_XENBUS,
    M_TEMP,
};

inline void* malloc(size_t size, int arena, int flags)
{
    return __bsd_malloc(size, flags);
}

inline void free(void* obj, int flags)
{
    free(obj);
}

inline char* strdup(const char* s, int bsd_malloc_arena)
{
    return strdup(s);
}


#endif

#endif

#endif
