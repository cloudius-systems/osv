/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _INTERNAL_ATOMIC_H
#define _INTERNAL_ATOMIC_H

#include <stdint.h>
#include <bsd/sys/cddl/compat/opensolaris/sys/types.h>
#include <machine/atomic.h>

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
    return atomic_fetchadd_int((unsigned int *)x, (unsigned int)v);
}

static inline void a_crash()
{
    __asm__ __volatile__( "1: msr daifset, #2; wfi; b 1b; " ::: "memory");
}


#endif /* _INTERNAL_ATOMIC_H */
