// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * OSv platform context for OpenZFS.
 *
 * This header is force-included (via -include) for all OpenZFS compilation
 * units to provide OS-specific definitions for OSv.
 *
 * IMPORTANT: We include netport.h early to get all the BSD compat
 * definitions (__printflike, M_* flags, struct thread, etc.) that
 * the compat headers expect. But we block solaris_uio.h to prevent
 * the conflicting 4-arg uiomove macro.
 */

#ifndef ZFS_CONTEXT_OS_H_
#define	ZFS_CONTEXT_OS_H_

/*
 * Block solaris_uio.h from being included by any compat header.
 * OpenZFS 2.3.6 uses its own zfs_uio_t wrapper instead.
 */
#define	_OPENSOLARIS_SYS_UIO_H_

/*
 * Block the old compat kmem.h - we provide our own standalone version
 * to avoid the netport.h -> solaris_uio.h chain issues.
 */
#define	_OPENSOLARIS_SYS_KMEM_H_

/*
 * Block the old compat proc.h - we provide our own standalone version
 * to avoid the PVM/PRIBIO dependency on FreeBSD priority.h.
 */
#define	_OPENSOLARIS_SYS_PROC_H_

/*
 * Block the old compat sunddi.h - we provide our own standalone version
 * to avoid sysevent.h chain.
 */
#define	_OPENSOLARIS_SYS_SUNDDI_H_

/*
 * Block the old compat cred.h - we provide our own standalone version.
 */
#define	_OPENSOLARIS_SYS_CRED_H_

/*
 * Block the old compat kstat.h - we provide our own standalone version.
 */
#define	_OPENSOLARIS_SYS_KSTAT_H_

/*
 * Block the old compat random.h - we provide our own standalone version.
 */
#define	_OPENSOLARIS_SYS_RANDOM_H_

/*
 * Block the old compat cmn_err.h - we provide our own standalone version.
 */
#define	_OPENSOLARIS_SYS_CMN_ERR_H_

/*
 * Block the old compat string.h - we provide our own standalone version.
 */
#define	_OPENSOLARIS_SYS_STRING_H_

/*
 * Block the old compat debug.h - we provide our own standalone version.
 */
#define	_OPENSOLARIS_SYS_DEBUG_H_

/*
 * Block the old contrib debug.h too.
 */
#define	_SYS_DEBUG_H

/*
 * Block the old compat taskq.h - we provide our own standalone version.
 */
#define	_OPENSOLARIS_SYS_TASKQ_H_

/*
 * Block the old compat vnode.h and the old contrib vnode.h.
 * These define xoptattr, xvattr, vsecattr structs that conflict with
 * OpenZFS's xvattr.h. OpenZFS's xvattr.h will be the sole provider
 * of these types. Our standalone SPL vnode.h provides what we need.
 */
#define	_OPENSOLARIS_SYS_VNODE_H_
#define	_SYS_VNODE_H

/*
 * Block the old compat vfs.h since it includes sys/vnode.h.
 * Our standalone SPL vfs.h provides what we need.
 */
#define	_OPENSOLARIS_SYS_VFS_H_

/*
 * Let the compat time.h through - it provides hrtime_t and gethrtime().
 */

/*
 * Block the old compat specdev.h.
 */
#define	_OPENSOLARIS_SYS_SPECDEV_H_

/*
 * Block the old compat zone.h - we define zone macros ourselves
 * and the compat zone.h has conflicting extern declarations.
 */
#define	_OPENSOLARIS_SYS_ZONE_H_

/*
 * Block the old compat misc.h - it declares `extern struct utsname utsname`
 * which conflicts with our `struct opensolaris_utsname utsname`.
 */
#define	_OPENSOLARIS_SYS_MISC_H_

/*
 * Ensure EXPORT_SYMBOL is defined as a no-op before any code uses it.
 * Some OpenZFS files use EXPORT_SYMBOL() without including sys/mod.h.
 */
#ifndef EXPORT_SYMBOL
#define	EXPORT_SYMBOL(x)
#endif

/*
 * Force _BSD_SOURCE so that OSv's sys/types.h defines BSD types
 * like caddr_t, u_int, u_char, etc.
 */
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

/*
 * Include standard headers needed throughout OpenZFS.
 * These provide base types that the SPL sys/types.h will use.
 */
#include <stddef.h>  /* for offsetof */
#include <stdint.h>  /* for int64_t, uint64_t, etc. */
#include <sys/ccompile.h>  /* for __init, __exit, EXPORT_SYMBOL */

/*
 * Get POSIX types from bits/alltypes.h BEFORE any other headers.
 * This avoids the OpenZFS SPL sys/types.h being found first.
 */
#define __NEED_id_t
#define __NEED_uid_t
#define __NEED_gid_t
#define __NEED_mode_t
#define __NEED_off_t
#define __NEED_pid_t
#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_time_t
#define __NEED_suseconds_t
#define __NEED_struct_timeval
#define __NEED_struct_timespec
#include <bits/alltypes.h>

/* Solaris time types */
typedef struct timespec timestruc_t;
typedef struct timespec timespec_t;

/*
 * CRITICAL: Define BSD type aliases and macros BEFORE including any headers.
 * The BSD compat headers (sys/time.h, netport.h, etc.) expect these.
 * These must be defined before <sys/time.h> is included.
 */
#ifndef __BSD_TYPES_DEFINED
#define __BSD_TYPES_DEFINED
typedef long long               longlong_t;
typedef unsigned long long      u_longlong_t;
typedef unsigned char           u_char;
typedef unsigned short          u_short;
typedef unsigned int            u_int;
typedef unsigned long           u_long;
typedef uint8_t                 u_int8_t;
typedef uint16_t                u_int16_t;
typedef uint16_t                __uint16_t;
typedef uint32_t                u_int32_t;
typedef uint32_t                __uint32_t;
typedef uint64_t                u_int64_t;
typedef uint64_t                __uint64_t;
typedef char *                  caddr_t;
typedef long long               quad_t;
typedef unsigned long long      u_quad_t;

/* BSD compat macros */
#define MAXNAMELEN              256
#endif

/* Now safe to include sys/types.h and sys/time.h */
#include <sys/time.h>  /* for additional time functions */

/*
 * panicstr must be declared before netport.h because the compat mutex.h
 * (included via netport.h) uses it in MUTEX_NOT_HELD.
 */
extern const char *panicstr;

/*
 * Now include netport.h to get all the BSD compat infrastructure:
 * struct thread, M_* flags, __printflike, panic(), MAXCPU, mp_ncpus, etc.
 */
#include <bsd/porting/netport.h>

/*
 * Now that struct uio is complete (from netport.h -> osv/uio.h),
 * provide the inline uio helper functions that OpenZFS needs.
 */
#include <sys/uio.h>
#include <string.h>
#include <fcntl.h>

static inline void
zfs_uio_setoffset(zfs_uio_t *uio, offset_t off)
{
	zfs_uio_offset(uio) = off;
}

static inline void
zfs_uio_setsoffset(zfs_uio_t *uio, offset_t off)
{
	zfs_uio_soffset(uio) = off;
}

static inline void
zfs_uio_advance(zfs_uio_t *uio, ssize_t size)
{
	zfs_uio_resid(uio) -= size;
	zfs_uio_offset(uio) += size;
}

static inline void
zfs_uio_init(zfs_uio_t *uio, struct uio *uio_s)
{
	memset(uio, 0, sizeof (zfs_uio_t));
	if (uio_s != NULL) {
		GET_UIO_STRUCT(uio) = uio_s;
		zfs_uio_soffset(uio) = uio_s->uio_offset;
	}
}

/*
 * Include rwlock types (krw_t, krwlock_t) - OpenZFS expects these
 * from the SPL layer but zfs_context.h doesn't include rwlock.h.
 */
#include <sys/rwlock.h>

/*
 * RW_NONE - some code uses this as a "no lock" sentinel.
 */
#ifndef RW_NONE
#define	RW_NONE		-1
#endif

/*
 * Cache line alignment attribute.
 * CACHE_LINE_SIZE is defined in netport.h as 128.
 */
#ifndef ____cacheline_aligned
#define	____cacheline_aligned	__attribute__((aligned(CACHE_LINE_SIZE)))
#endif

/*
 * SDT probes and SET_ERROR macro.
 * Must be included before any ZFS code that uses SET_ERROR().
 */
#include <sys/sdt.h>

/*
 * siginfo_t for arc.h
 */
#include <signal.h>

/*
 * zfs_fallthrough - GCC/Clang fallthrough attribute.
 */
#ifndef zfs_fallthrough
#define	zfs_fallthrough		__attribute__((__fallthrough__))
#endif

/*
 * Thread-specific data - implemented via POSIX pthread TLS.
 * OSv fully supports pthreads so pthread_key_t works correctly.
 * uint_t and pthread_key_t are both unsigned int on OSv/x86-64.
 */
#include <pthread.h>
#define	tsd_create(keyp, destructor) \
	pthread_key_create((pthread_key_t *)(keyp), (destructor))
#define	tsd_destroy(keyp) \
	pthread_key_delete((pthread_key_t)(*(keyp)))
#define	tsd_get(key) \
	pthread_getspecific((pthread_key_t)(key))
#define	tsd_set(key, value) \
	pthread_setspecific((pthread_key_t)(key), (void *)(value))

#define	fm_panic	panic

/*
 * OSv debug/log macros.
 */
extern int zfs_debug_level;
#define	ZFS_LOG(lvl, ...) do {					\
	if (((lvl) & 0xff) <= zfs_debug_level) {		\
		printf("%s:%u[%d]: ", __func__, __LINE__, (lvl)); \
		printf(__VA_ARGS__);				\
		printf("\n");					\
	}							\
} while (0)

/*
 * Time conversion macros.
 * hz is already defined by netport.h as (1000L).
 */
#define	MSEC_TO_TICK(msec)	(howmany((hrtime_t)(msec) * hz, MILLISEC))

/*
 * Filesystem transaction cookies (no-op on OSv).
 */
typedef int fstrans_cookie_t;
#define	spl_fstrans_mark()		(0)
#define	spl_fstrans_unmark(x)		((void)x)

/*
 * Signal checking (OSv is a unikernel, no signal delivery to worry about).
 */
#define	signal_pending(x)		(0)
#define	issig()				(0)

/*
 * Current thread macros.
 */
#define	current		curthread
#define	thread_join(x)
#define	getcomm()	"osv"

/*
 * Stack size check.
 */
#define	HAVE_LARGE_STACKS	1

/*
 * Memory reclaim thread check (no reclaim thread in OSv).
 */
#define	current_is_reclaim_thread()	(0)

/*
 * Delay function: sleep for a given number of clock ticks.
 */
extern void delay(clock_t ticks);

/*
 * Mutex extensions.
 */
#define	NESTED_SINGLE		1
#define	mutex_enter_nested(mp, class)	mutex_enter(mp)
#define	mutex_enter_interruptible(mp)	(mutex_enter(mp), 0)

/*
 * Condvar extras.
 */
#define	CALLOUT_FLAG_ABSOLUTE	0x2

/*
 * taskq_create_sysdc wrapper.
 * Note: must not conflict with the extern declaration in taskq.h.
 */
#define	taskq_create_sysdc(a, b, d, e, p, dc, f) \
	((void) sizeof (dc), taskq_create(a, b, maxclsyspri, d, e, f))

/*
 * thread_create_named - some OpenZFS code uses this variant.
 */
#define	thread_create_named(name, stk, stksize, func, arg, len, pp, state, pri) \
	thread_create(stk, stksize, func, arg, len, pp, state, pri)

/*
 * CPU_SEQID for per-CPU data structures.
 */
extern unsigned int sched_current_cpu(void);
#define	CPU_SEQID		sched_current_cpu()
#define	CPU_SEQID_UNSTABLE	CPU_SEQID

/*
 * getcpuid - alias for CPU_SEQID.
 */
#define	getcpuid()		sched_current_cpu()

/*
 * Write taskq priority.
 */
#define	wtqclsyspri		maxclsyspri

/*
 * Process/zone macros.
 */
struct proc;
extern struct proc proc0;
#define	curproc			(&proc0)
#define	GLOBAL_ZONEID		0
#define	zone_dataset_visible(x, y)	(1)
#define	INGLOBALZONE(z)		(1)

/*
 * utsname_t - hostname information.
 * OSv's compat layer defines a global `utsname` variable (struct).
 * OpenZFS code calls utsname()->nodename (function-style).
 * We provide a function that returns a pointer to the global.
 */
typedef struct opensolaris_utsname	utsname_t;
extern utsname_t *osv_utsname(void);
#define	utsname()	osv_utsname()

/* panicstr already declared before netport.h include */

/*
 * random_in_range - return a pseudo-random number in [0, range).
 */
extern int random_get_pseudo_bytes(uint8_t *ptr, size_t len);
static inline uint32_t
random_in_range(uint32_t range)
{
	uint32_t r;

	if (range <= 1)
		return (0);

	(void) random_get_pseudo_bytes((uint8_t *)&r, sizeof (r));
	return (r % range);
}

/*
 * FKIOCTL: flag indicating the ioctl buffer pointer is in unified address
 * space (no copyin/copyout needed).  On OSv there is no user/kernel split
 * so all ioctl calls from libzfs are effectively "kernel" pointers.
 * Must match the value used by FreeBSD/Linux SPL (0x80000000).
 */
#ifndef FKIOCTL
#define	FKIOCTL	0x80000000
#endif

/*
 * Open flags - ensure they're available.
 * OSv should define these, but they may not be visible to kernel code
 * without including <fcntl.h>.
 */
#ifndef O_RDONLY
#define	O_RDONLY	0x0000
#endif
#ifndef O_WRONLY
#define	O_WRONLY	0x0001
#endif
#ifndef O_RDWR
#define	O_RDWR		0x0002
#endif
#ifndef O_SYNC
#define	O_SYNC		0x0080
#endif
#ifndef O_DSYNC
#define	O_DSYNC		O_SYNC
#endif
#ifndef O_LARGEFILE
#define	O_LARGEFILE	0
#endif

/*
 * Endianness - detect at compile time.
 */
#if !defined(_ZFS_LITTLE_ENDIAN) && !defined(_ZFS_BIG_ENDIAN)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define	_ZFS_LITTLE_ENDIAN
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define	_ZFS_BIG_ENDIAN
#endif
#endif

/*
 * Unicode/text conversion constants and u8_textprep_str() are provided
 * by the compat layer's u8_textprep.h. No need to redefine them here.
 */

/*
 * ATTR_* -> AT_* mappings (matching FreeBSD ccompile.h).
 * These are defined in our vnode.h (AT_ constants from osv/vnode_attr.h),
 * but OpenZFS code uses ATTR_* names from the Linux side.
 */
#ifndef ATTR_CTIME
#define	ATTR_CTIME	AT_CTIME
#endif
#ifndef ATTR_MTIME
#define	ATTR_MTIME	AT_MTIME
#endif
#ifndef ATTR_ATIME
#define	ATTR_ATIME	AT_ATIME
#endif

/*
 * Root pool import for boot.
 */
extern int spa_import_rootpool(const char *name, bool checkpointrewind);

/*
 * simd_stat - declared here, implemented in zcommon/simd_stat.c
 */
extern void simd_stat_init(void);
extern void simd_stat_fini(void);

/*
 * XDR function compatibility.
 * The OpenZFS xdr_array call passes a pointer to xdrproc_t,
 * which may have different signedness. Suppress the warning.
 */

#endif /* ZFS_CONTEXT_OS_H_ */
