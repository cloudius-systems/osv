// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * OpenZFS-OSv Compatibility Shim
 *
 * This file bridges the OpenZFS platform API expectations with
 * OSv kernel interfaces. It provides implementations of functions
 * that OpenZFS expects from the OS layer but that don't have
 * direct equivalents in OSv.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/arc.h>
#include <sys/abd.h>
#include <sys/abd_impl.h>
#include <stdlib.h>
#include <string.h>

/*
 * freemem - free page count used by OpenZFS ARC sizing.
 * physmem is exported from loader.elf (bsd/porting/netport.cc).
 * freemem is defined here; arc_os.c in the same shared library
 * satisfies its "extern unsigned long freemem;" from this definition.
 * Initialized in zfs_compat_init() to physmem/4 as a conservative
 * first approximation.  A future enhancement can hook into the OSv
 * page allocator to keep this live.
 */
extern unsigned long physmem;
unsigned long freemem = 0;

/*
 * OSv VM pressure detection.
 * Returns true when the system is under memory pressure
 * and ARC should shrink.
 */
extern boolean_t vm_throttling_needed(void);

/*
 * Pool sync on last unmount.
 * Called from zfs_vfsops when the last ZFS dataset is unmounted.
 */
extern void spa_sync_allpools(void);

/*
 * Dentry release for unmount.
 * OSv uses dentries instead of FreeBSD vnodes.
 */
extern void release_mp_dentries(void *vfsp);

/*
 * ZFS driver state tracking.
 * Canonical definition is in fs/zfs/zfs_null_vfsops.cc (loader.elf).
 * Do NOT define it here; a duplicate definition with different type
 * (boolean_t vs bool) causes linker confusion and PC32 reloc failures.
 */

/*
 * nocacheflush tunable -- now defined by OpenZFS vdev.c via ZFS_MODULE_PARAM.
 */

/*
 * Active filesystem count for pool sync optimization.
 */
uint32_t zfs_active_fs_count = 0;

/*
 * panicstr - pointer to panic message (NULL when not panicking).
 * Used by compat mutex.h MUTEX_NOT_HELD macro.
 */
const char *panicstr = NULL;

/*
 * utsname wrapper for OpenZFS.
 *
 * libc/misc/uname.c exports a POSIX `struct utsname utsname` with char arrays.
 * OpenZFS uses `struct opensolaris_utsname` (utsname_t) with const char *
 * pointer fields — the two structs are layout-incompatible.
 * Reading nodename via the pointer layout from the POSIX struct gives NULL
 * (bytes 8-15 of "Linux\0..."), causing fnvlist_add_string() to VERIFY-fail.
 *
 * Fix: maintain a separate static utsname_t with pointer fields and return it.
 */
static utsname_t osv_utsname_data = {
	.sysname  = "OSv",
	.nodename = "osv",
	.release  = "0",
	.machine  = "x86_64",
};
utsname_t *
osv_utsname(void)
{
	return (&osv_utsname_data);
}

/*
 * spl_panic - core assertion failure handler.
 * Called by VERIFY/ASSERT macros.
 */
void
spl_panic(const char *file, const char *func, int line,
    const char *fmt, ...)
{
	va_list ap;

	printf("SPL PANIC at %s:%d:%s(): ", file, line, func);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	panic("spl_panic");
}

void
spl_dumpstack(void)
{
	/* Stack dump not yet implemented on OSv */
}

/*
 * assfail/assfail3 - provided by opensolaris_cmn_err.c, not duplicated here.
 */

/*
 * delay - sleep for a number of clock ticks.
 * hz is defined as (1000L) by netport.h (included via zfs_context.h).
 */
void
delay(clock_t ticks)
{
	/* Convert ticks to microseconds and sleep */
	if (ticks > 0) {
		struct timespec ts;
		uint64_t usec = (uint64_t)ticks * 1000000 / hz;
		ts.tv_sec = usec / 1000000;
		ts.tv_nsec = (usec % 1000000) * 1000;
		nanosleep(&ts, NULL);
	}
}

/*
 * zfs_debug_level - now defined in sysctl_os.c, not duplicated here.
 */

/*
 * kmem_scnprintf - snprintf that returns characters written (not would-write).
 */
int
kmem_scnprintf(char *restrict str, size_t size,
    const char *restrict fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(str, size, fmt, ap);
	va_end(ap);

	if (n >= (int)size)
		n = (int)size - 1;
	if (n < 0)
		n = 0;
	return (n);
}

/*
 * panic - abort the system with a printf-like message.
 * OpenZFS declares this as extern void panic(const char *, ...) in
 * zfs_context.h.  The BSD compat layer defines it as a macro (via
 * netport.h) so code compiled against netport.h uses the macro path;
 * code compiled against the OpenZFS headers (i.e. everything in
 * libsolaris.so) resolves it through this C function via PLT.
 *
 * We must undef the netport.h macro before the function definition,
 * otherwise the compiler expands our function definition header as the
 * macro body.  Code above this point (spl_panic) continues to use the
 * macro, which is fine since it has the same effect.
 */
#undef panic
void __attribute__((noreturn))
panic(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	abort();
	__builtin_unreachable();
}

/*
 * zero_region - zero a memory region.
 * Used by OpenZFS to wipe buffers before reuse.
 */
void
zero_region(void *addr, size_t len)
{
	memset(addr, 0, len);
}

/*
 * kmem_vasprintf - kernel vasprintf (allocates via malloc).
 * Signature matches external/openzfs/include/os/osv/spl/sys/kmem.h:
 *   char *kmem_vasprintf(const char *fmt, va_list ap)
 * Returns an allocated string that the caller must free with kmem_strfree().
 */
char *
kmem_vasprintf(const char *fmt, va_list ap)
{
	char *buf = NULL;
	(void) vasprintf(&buf, fmt, ap);
	return (buf);
}

/*
 * ddi_strtoll - OpenSolaris string-to-long-long conversion.
 * Signature matches external/openzfs/include/os/osv/spl/sys/string.h:
 *   int ddi_strtoll(const char *str, char **nptr, int base, longlong_t *)
 * Returns 0 on success, EINVAL/ERANGE on error.
 */
int
ddi_strtoll(const char *str, char **nptr, int base, longlong_t *result)
{
	char *endptr;
	int err = 0;

	*result = strtoll(str, &endptr, base);
	if (endptr == str)
		err = EINVAL;
	if (nptr != NULL)
		*nptr = endptr;
	return (err);
}

/*
 * random_get_pseudo_bytes - generate pseudo-random bytes.
 * The OpenZFS headers define this as a macro that expands to read_random().
 * However, some compilation units (compiled before the macro is defined via
 * the -include zfs_context_os.h preinclude) reference it as a function
 * symbol. We provide the real function here, after undefining the macro
 * so that the function definition isn't itself macro-expanded.
 */
extern int read_random(void *, int);

#undef random_get_pseudo_bytes
int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	return (read_random(ptr, (int)len));
}

/* ------------------------------------------------------------------ */
/* Embedded-entry (taskq_ent_t) API                                    */
/* Defined here (not in opensolaris_taskq.c) because taskq_ent_t is   */
/* only visible with the full OpenZFS include paths.                   */
/* ------------------------------------------------------------------ */

/*
 * taskq_init_ent - initialize a caller-owned task entry.
 */
void
taskq_init_ent(taskq_ent_t *e)
{
	memset(e, 0, sizeof(*e));
}

/*
 * taskq_empty_ent - true if the entry is not currently queued.
 * Mirrors the FreeBSD spl: the embedded task's ta_pending is set to 1 by
 * taskqueue_enqueue() and cleared to 0 by taskqueue_run() once the handler
 * has run, so it is the authoritative "is this entry in flight" flag.
 */
int
taskq_empty_ent(taskq_ent_t *e)
{
	return (e->tqent_ostask.ost_task.ta_pending == 0);
}

/*
 * taskq_dispatch_ent - dispatch using the caller's embedded entry.
 * Routes through taskq_dispatch_safe(), which initializes and enqueues the
 * entry's own ostask without ever freeing it.  The entry is owned by the
 * caller (it lives inside a zio_t/dbuf_t/etc.), matching upstream FreeBSD
 * semantics.  tqent_id is the address of the embedded ostask, which stays
 * valid for the entry's lifetime, so taskq_wait_id() never dereferences
 * freed memory.
 */
void
taskq_dispatch_ent(taskq_t *tq, task_func_t func, void *arg, uint_t flags,
    taskq_ent_t *e)
{
	e->tqent_func = func;
	e->tqent_arg  = arg;
	e->tqent_id   = taskq_dispatch_safe(tq, func, arg, flags,
	    &e->tqent_ostask);
}
