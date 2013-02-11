#ifndef NETPORT_H
#define NETPORT_H

#include <stdio.h>
#include <stdint.h>
#include <memory.h>

#include <sys/types.h>
#include <sys/queue.h>

/* Defines how many ticks are in 1 minute */
#ifndef hz
#define hz (1)
#endif

#define MALLOC_DEFINE(...)

#define SYSINIT(...)
#define SYSUNINIT(...)
#define SYSCTL_NODE(...)
#define SYSCTL_UINT(...)
#define SYSCTL_INT(...)
#define TUNABLE_INT(...)

#define __NO_STRICT_ALIGNMENT

/* FIXME: struct socket is here for compilation purposes only */

/*-
 * Locking key to struct socket:
 * (a) constant after allocation, no locking required.
 * (b) locked by SOCK_LOCK(so).
 * (c) locked by SOCKBUF_LOCK(&so->so_rcv).
 * (d) locked by SOCKBUF_LOCK(&so->so_snd).
 * (e) locked by ACCEPT_LOCK().
 * (f) not locked since integer reads/writes are atomic.
 * (g) used only as a sleep/wakeup address, no value.
 * (h) locked by global mutex so_global_mtx.
 */
struct socket {
    int so_count;       /* (b) reference count */
    short   so_type;        /* (a) generic type, see socket.h */
    short   so_options;     /* from socket call, see socket.h */
    short   so_linger;      /* time to linger while closing */
    short   so_state;       /* (b) internal state flags SS_* */
    int so_qstate;      /* (e) internal state flags SQ_* */
    void    *so_pcb;        /* protocol control block */
    struct  vnet *so_vnet;      /* network stack instance */
    struct  protosw *so_proto;  /* (a) protocol handle */
/*
 * Variables for connection queuing.
 * Socket where accepts occur is so_head in all subsidiary sockets.
 * If so_head is 0, socket is not related to an accept.
 * For head socket so_incomp queues partially completed connections,
 * while so_comp is a queue of connections ready to be accepted.
 * If a connection is aborted and it has so_head set, then
 * it has to be pulled out of either so_incomp or so_comp.
 * We allow connections to queue up based on current queue lengths
 * and limit on number of queued connections for this socket.
 */
    struct  socket *so_head;    /* (e) back pointer to listen socket */
    TAILQ_HEAD(, socket) so_incomp; /* (e) queue of partial unaccepted connections */
    TAILQ_HEAD(, socket) so_comp;   /* (e) queue of complete unaccepted connections */
    TAILQ_ENTRY(socket) so_list;    /* (e) list of unaccepted connections */
    u_short so_qlen;        /* (e) number of unaccepted connections */
    u_short so_incqlen;     /* (e) number of unaccepted incomplete
                       connections */
    u_short so_qlimit;      /* (e) max number queued connections */
    short   so_timeo;       /* (g) connection timeout */
    u_short so_error;       /* (f) error affecting connection */
    struct  sigio *so_sigio;    /* [sg] information for async I/O or
                       out of band data (SIGURG) */
    u_long  so_oobmark;     /* (c) chars to oob mark */
    TAILQ_HEAD(, aiocblist) so_aiojobq; /* AIO ops waiting on socket */

    struct  ucred *so_cred;     /* (a) user credentials */
    struct  label *so_label;    /* (b) MAC label for socket */
    struct  label *so_peerlabel;    /* (b) cached MAC label for peer */
    /* NB: generation count must not be first. */
    int so_gencnt;     /* (h) generation count */
    void    *so_emuldata;       /* (b) private data for emulators */
    struct so_accf {
        struct  accept_filter *so_accept_filter;
        void    *so_accept_filter_arg;  /* saved filter args */
        char    *so_accept_filter_str;  /* saved user args */
    } *so_accf;
    /*
     * so_fibnum, so_user_cookie and friends can be used to attach
     * some user-specified metadata to a socket, which then can be
     * used by the kernel for various actions.
     * so_user_cookie is used by ipfw/dummynet.
     */
    int so_fibnum;      /* routing domain for this socket */
    uint32_t so_user_cookie;
};

/* pseudo-errors returned inside kernel to modify return to process */
#define EJUSTRETURN (-2)        /* don't modify regs, just return */
#define ENOIOCTL    (-3)        /* ioctl not handled by this layer */
#define EDIRIOCTL   (-4)        /* do direct ioctl in GEOM */

#define sx_slock(...) do{}while(0)
#define sx_sunlock(...) do{}while(0)
#define sx_xlock(...) do{}while(0)
#define sx_xunlock(...) do{}while(0)

#define getmicrotime(...) do{}while(0)

#ifndef time_uptime
#define time_uptime (1)
#endif

#define __packed    __attribute__((__packed__))

#ifndef INET
    #define INET (1)
#endif

#define panic(...) abort()

#define log(x, fmt, ...) do {} while(0)

#ifndef __BSD_VISIBLE
    #define __BSD_VISIBLE (1)
#endif

typedef __uint8_t   __sa_family_t;

#define PAGE_SIZE (4096)

void abort(void);
void* malloc(size_t size);
void free(void* object);
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

#ifndef _KERNEL
    #define _KERNEL do{}while(0)
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

#endif
