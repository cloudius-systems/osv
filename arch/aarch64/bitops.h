/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_BITOPS_H_
#define ARCH_BITOPS_H_
static inline unsigned int
bsrl(unsigned int mask)
{
    unsigned int result;
    asm volatile("clz %1,%0" : "=r" (result) : "r" (mask));
    return result;
}

static inline unsigned long
bsrq(unsigned long mask)
{
    unsigned long result;
    asm volatile("clz %1,%0" : "=r" (result) : "r" (mask));
    return result;
}

static inline int
fls(int mask)
{
    return (mask == 0 ? mask : (int)bsrl((unsigned int)mask) + 1);
}
#endif /* ARCH_BITOPS_H */
