/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _INTERNAL_ATOMIC_H
#define _INTERNAL_ATOMIC_H

#include <stdint.h>
/*
 * These two headers pull in OSv kernel-only machinery (the FreeBSD-derived
 * machine/atomic.h and the old bsd/cddl opensolaris sys/types.h). They are only
 * on the include path for kernel/bsd objects. Userspace translation units that
 * resolve <atomic.h> to this file (e.g. the OpenZFS libspl/libzfs sources on
 * aarch64) do not have them available and do not need them, so gate them on the
 * kernel build. The x64 variant of this file has no such includes; this change
 * is aarch64-only and leaves the x86_64 build byte-identical.
 */
#if defined(_KERNEL) || defined(__OSV_CORE__)
#include <bsd/sys/cddl/compat/opensolaris/sys/types.h>
#include <machine/atomic.h>
#endif

static inline int a_ctz_64(register uint64_t x)
{
	register uint64_t r;
	__asm__ __volatile__ ("rbit %0, %0; clz %1, %0" : "+r"(x), "=r"(r));
	return r;
}

static inline int a_ctz_l(unsigned long x)
{
	return a_ctz_64(x);
}

static inline int a_fetch_add(volatile int *x, int v)
{
#if defined(_KERNEL) || defined(__OSV_CORE__)
    return atomic_fetchadd_int((unsigned int *)x, (unsigned int)v);
#else
    /* Userspace: machine/atomic.h is unavailable; use the LSE/LL-SC builtin. */
    return __atomic_fetch_add(x, v, __ATOMIC_SEQ_CST);
#endif
}

static inline void a_crash()
{
    __asm__ __volatile__( "1: msr daifset, #2; wfi; b 1b; " ::: "memory");
}


#endif /* _INTERNAL_ATOMIC_H */
