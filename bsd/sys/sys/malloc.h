#ifndef _BSD_MALLOC_H
#define _BSD_MALLOC_H
#include <bsd/porting/mmu.h>
#include <bsd/porting/netport.h>
// just our malloc impl.
#include <malloc.h>
#include "param.h" // BSD malloc includes this, so some files do not.
                   // Also differentiate from include/api/sys/param.h
#include "priority.h"

#ifdef __FBSDID
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
#define malloc(x, y, z) __bsd_malloc(x, z)
#define free(x, y) free(x)
#define strdup(x, y) strdup(x)
#endif
#endif
