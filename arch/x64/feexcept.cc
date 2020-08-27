/*
 * Copyright (C) 2017 ScyllaDB, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include <fenv.h>
#include <__fenv.h>

int feenableexcept(int mask)
{
    // The feenableexcept() manual page suggests that -1 should be returned
    // on failure, and theoretically this could include the case that invalid
    // bits are passed in mask. But in practice, the Linux implementation
    // simply ignores invalid bits, and never returns -1.
    mask &= FE_ALL_EXCEPT;

    // Update the the x87 control word. Note that:
    // 1. We need to leave the other bits (non-exception related) unchanged.
    // 2. A 1 bit means disable exception (opposite of our "mask")
    // 3. This function is supposed to only enable exceptions.
    u16 cw;
    asm volatile("fstcw %0" : "=m"(cw));
    // Save the previous set of enabled exceptions, for returning later.
    int ret = (~cw) & FE_ALL_EXCEPT;
    cw &= ~mask;
    asm volatile("fldcw %0" : : "m"(cw));

    // Also update the SSE control and status register (here the relevant
    // bits start at bit 7).
    u32 csr;
    asm volatile("stmxcsr %0" : "=m"(csr));
    csr &= ~(mask << 7);
    asm volatile("ldmxcsr %0" : : "m"(csr));

    return ret;
}

int fedisableexcept(int mask)
{
    mask &= FE_ALL_EXCEPT;

    u16 cw;
    asm volatile("fstcw %0" : "=m"(cw));
    int ret = (~cw) & FE_ALL_EXCEPT;
    cw |= mask;
    asm volatile("fldcw %0" : : "m"(cw));

    u32 csr;
    asm volatile("stmxcsr %0" : "=m"(csr));
    csr |= mask << 7;
    asm volatile("ldmxcsr %0" : : "m"(csr));

    return ret;
}

int fegetexcept()
{
    u16 cw;
    asm volatile("fstcw %0" : "=m"(cw));
    return (~cw) & FE_ALL_EXCEPT;
}
