#ifndef NETPORT_H
#define NETPORT_H

#include <osv/uio.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <time.h>
#include <pthread.h>
#include <osv/debug.h>
#define __NEED_socklen_t
#include <bits/alltypes.h>

__BEGIN_DECLS

typedef unsigned char bsd_sa_family_t;

typedef int accmode_t;

#define MOD_LOAD (1)
#define MOD_UNLOAD (2)

#define MAXCOMLEN       19 

/* maximum common x86 L1 size */
#define CACHE_LINE_SIZE		128

#define __offsetof(type, field)  __builtin_offsetof(type, field)

#define __containerof(x, s, m) ({                                       \
	const volatile __typeof(((s *)0)->m) *__x = (x);                \
	__DEQUALIFY(s *, (const volatile char *)__x - __offsetof(s, m));\
})

#define __dead2         __attribute__((__noreturn__))
#define __pure2         __attribute__((__const__))
#define __unused2       __attribute__((__unused__))
#define __used          __attribute__((__used__))
#define __packed        __attribute__((__packed__))
#define __aligned(x)    __attribute__((__aligned__(x)))
#define __section(x)    __attribute__((__section__(x)))

#define __predict_false(x) (x)
#define __predict_true(x) (x)

struct proc {
};

extern struct proc proc0;

/*
 * We use this as a fake opaque C type for passing around sched::thread
 * pointers in C code.
 */
struct thread {
};

void init_maxsockets(void *ignored);

int osv_curtid(void);

#define priv_check_cred(...)        (0)
#define priv_check(...)        (0)

#define FEATURE(...)

#include <sys/types.h>

/* Implemented in bsd/sys/kern/subr_hash.c */
struct malloc_type;
void    hashdestroy(void *, struct malloc_type *, u_long);
void    *hashinit(int count, struct malloc_type *type, u_long *hashmask);
void    *hashinit_flags(int count, struct malloc_type *type,
    u_long *hashmask, int flags);
#define HASH_NOWAIT 0x00000001
#define HASH_WAITOK 0x00000002

int read_random(void*, int);
void domaininit(void *dummy);
void domainfinalize(void *dummy);

#define DECLARE_MODULE(...)
#define MODULE_VERSION(...)

#define devctl_notify(...) do{}while(0)

void getmicrotime(struct timeval *tvp);
void getmicrouptime(struct timeval *tvp);
int tvtohz(struct timeval *tv);

/* Returns the current time in ticks (there's hz ticks in 1 second) */
int get_ticks(void);
#define ticks (get_ticks())

extern int tick;

#define TSECOND (1000000000L)
#define TMILISECOND (1000000L)

/* Defines how many ticks are in 1 second */
#define hz (1000000L)
#define ticks2ns(ticks) (ticks * (TSECOND / hz))
#define ns2ticks(ns)    (ns / (TSECOND / hz))

#define MALLOC_DEFINE(...)
#define MALLOC_DECLARE(...)

#define SYSINIT(...)
#define SYSUNINIT(...)
#define SYSCTL_NODE(...)
#define SYSCTL_DECL(...)
#define SYSCTL_UINT(...)
#define SYSCTL_INT(...)
#define SYSCTL_ULONG(...)
#define SYSCTL_UQUAD(...)
#define SYSCTL_PROC(...)
#define SYSCTL_OPAQUE(...)
#define SYSCTL_STRING(...)
#define SYSCTL_STRUCT(...)
#define SYSCTL_OID(...)
#define TUNABLE_INT(...)
#define TUNABLE_ULONG(...)
#define TUNABLE_QUAD(...)
#define TUNABLE_INT_FETCH(...)

#define __NO_STRICT_ALIGNMENT

/* pseudo-errors returned inside kernel to modify return to process */
#define EJUSTRETURN (-2)        /* don't modify regs, just return */
#define ENOIOCTL    (-3)        /* ioctl not handled by this layer */
#define EDIRIOCTL   (-4)        /* do direct ioctl in GEOM */

/* FIXME: TODO - Implement... */
#ifndef time_uptime
#define time_uptime (1)
#endif

#define __packed    __attribute__((__packed__))

#ifndef INET
    #define INET (1)
#endif

#define panic(...) do { tprintf_e("bsd-panic", __VA_ARGS__); \
                        abort(); } while(0)

#define bsd_log(x, ...) tprintf_e("bsd-log", __VA_ARGS__)

#define PAGE_MASK (PAGE_SIZE-1)

void abort(void);
size_t strlcat(char *dst, const char *src, size_t siz);
size_t strlcpy(char *dst, const char *src, size_t siz);

/* Tracing... */
#define CTR0(m, d)          (void)0
#define CTR1(m, d, p1)          (void)0
#define CTR2(m, d, p1, p2)      (void)0
#define CTR3(m, d, p1, p2, p3)      (void)0
#define CTR4(m, d, p1, p2, p3, p4)  (void)0
#define CTR5(m, d, p1, p2, p3, p4, p5)  (void)0
#define CTR6(m, d, p1, p2, p3, p4, p5, p6)  (void)0

#ifndef NULL
    #define NULL (0)
#endif

#define KASSERT(exp,msg) assert(exp)

#define bsd_min(a, b) ((a) < (b) ? (a) : (b))
#define bsd_max(a, b) ((a) > (b) ? (a) : (b))

/*
* flags to malloc.
*/
#define M_NOWAIT    0x0001      /* do not block */
#define M_WAITOK    0x0002      /* ok to block */
#define M_ZERO      0x0100      /* bzero the allocation */
#define M_NOVM      0x0200      /* don't ask VM for pages */
#define M_USE_RESERVE   0x0400      /* can alloc out of reserve memory */
#define M_NODUMP    0x0800      /* don't dump pages in this allocation */


#define bcopy(src, dst, len)    memcpy((dst), (src), (len))
#define bzero(buf, size)    memset((buf), 0, (size))
#define bcmp(b1, b2, len)   (memcmp((b1), (b2), (len)) != 0)


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

#ifdef INVARIANTS
#undef INVARIANTS
#endif

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


int copyin(const void *uaddr, void *kaddr, size_t len);
int copyout(const void *kaddr, void *uaddr, size_t len);
int copystr(const void *kfaddr, void *kdaddr, size_t len, size_t *done);
int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done);

typedef __uint32_t  intrmask_t; /* Interrupt mask (spl, xxx_imask...) */

/* Stubs for obsolete functions that used to be for interrupt management */
static __inline void        spl0(void)      { return; }
static __inline intrmask_t  splbio(void)        { return 0; }
static __inline intrmask_t  splcam(void)        { return 0; }
static __inline intrmask_t  splclock(void)      { return 0; }
static __inline intrmask_t  splhigh(void)       { return 0; }
static __inline intrmask_t  splimp(void)        { return 0; }
static __inline intrmask_t  splnet(void)        { return 0; }
static __inline intrmask_t  splsoftcam(void)    { return 0; }
static __inline intrmask_t  splsoftclock(void)  { return 0; }
static __inline intrmask_t  splsofttty(void)    { return 0; }
static __inline intrmask_t  splsoftvm(void)     { return 0; }
static __inline intrmask_t  splsofttq(void)     { return 0; }
static __inline intrmask_t  splstatclock(void)  { return 0; }
static __inline intrmask_t  spltty(void)        { return 0; }
static __inline intrmask_t  splvm(void)     { return 0; }
static __inline void        splx(intrmask_t ipl)   { return; }

/* must match max_cpus in include/sched.hh */
#define MAXCPU		(sizeof(unsigned long) * 8)

extern unsigned smp_processors;
#define mp_ncpus smp_processors

/*
 * OSv: Copied from kern_time.c
 *
 * ppsratecheck(): packets (or events) per second limitation.
 *
 * Return 0 if the limit is to be enforced (e.g. the caller
 * should drop a packet because of the rate limitation).
 *
 * maxpps of 0 always causes zero to be returned.  maxpps of -1
 * always causes 1 to be returned; this effectively defeats rate
 * limiting.
 *
 * Note that we maintain the struct timeval for compatibility
 * with other bsd systems.  We reuse the storage and just monitor
 * clock ticks for minimal overhead.
 */
int ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps);

/*
 * ratecheck(): simple time-based rate-limit checking.
 */
int ratecheck(struct timeval *lasttime, const struct timeval *mininterval);

int get_cpuid(void);

size_t get_physmem(void);
#define physmem		get_physmem()

#define UID_ROOT	0
#define UID_NOBODY	65534
#define	GID_NOBODY	65534

#define UID_MAX		UINT_MAX
#define GID_MAX		UINT_MAX

__END_DECLS

#endif
