#ifndef NETPORT_H
#define NETPORT_H

#include <stdio.h>
#include <stdint.h>
#include <memory.h>

#include <sys/types.h>

#define PAGE_SIZE (4096)

void* malloc(size_t size);
void free(void* object);

/* Tracing... */
#define CTR0(m, d)          (void)0
#define CTR1(m, d, p1)          (void)0
#define CTR2(m, d, p1, p2)      (void)0
#define CTR3(m, d, p1, p2, p3)      (void)0
#define CTR4(m, d, p1, p2, p3, p4)  (void)0
#define CTR5(m, d, p1, p2, p3, p4, p5)  (void)0
#define CTR6(m, d, p1, p2, p3, p4, p5, p6)  (void)0

#ifndef _KERNEL
    #define _KERNEL
#endif

#ifndef NULL
    #define NULL (0)
#endif

#define KASSERT(exp,msg) do {} while (0)

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/*
* flags to malloc.
*/
#define M_NOWAIT    0x0001      /* do not block */
#define M_WAITOK    0x0002      /* ok to block */
#define M_ZERO      0x0100      /* bzero the allocation */
#define M_NOVM      0x0200      /* don't ask VM for pages */
#define M_USE_RESERVE   0x0400      /* can alloc out of reserve memory */
#define M_NODUMP    0x0800      /* don't dump pages in this allocation */


#define bzero(poi,len) memset(poi,0,len)

typedef uintptr_t __uintptr_t;

#ifndef __DECONST
#define __DECONST(type, var)    ((type)(__uintptr_t)(const void *)(var))
#endif

#ifndef __DEVOLATILE
#define __DEVOLATILE(type, var) ((type)(__uintptr_t)(volatile void *)(var))
#endif

#ifndef __DEQUALIFY
#define __DEQUALIFY(type, var)  ((type)(__uintptr_t)(const volatile void *)(var))
#endif

static __inline void panic(const char* msg)
{
    return;
}

#ifdef INVARIANTS
#undef INVARIANTS
#endif

/* Segment flag values. */
enum uio_seg {
    UIO_USERSPACE,      /* from user data space */
    UIO_SYSSPACE,       /* from system space */
    UIO_NOCOPY      /* don't copy, already in object */
};

enum    uio_rw { UIO_READ, UIO_WRITE };

struct uio {
    struct  iovec *uio_iov;     /* scatter/gather list */
    int uio_iovcnt;     /* length of scatter/gather list */
    off_t   uio_offset;     /* offset in target object */
    ssize_t uio_resid;      /* remaining bytes to process */
    enum    uio_seg uio_segflg; /* address space */
    enum    uio_rw uio_rw;      /* operation */
    struct  thread *uio_td;     /* owner */
};

int uiomove(void *cp, int n, struct uio *uio);

struct malloc_type {
    int unused;
};

#define __printflike(m, n) __attribute__((format(printf,m,n)))
/*
 * Unusual type definitions.
 */
typedef __builtin_va_list   __va_list;  /* internally known to gcc */

/* Max number conversion buffer length: a u_quad_t in base 2, plus NUL byte. */
#define MAXNBUF (sizeof(intmax_t) * NBBY + 1)

#endif
