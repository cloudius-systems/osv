/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_BITOPS_H_
#define ARCH_BITOPS_H_
static inline unsigned int
bsrl(unsigned int mask)
{
    return sizeof(mask) * 8 - __builtin_clz(mask) - 1;
}

static inline unsigned long
bsrq(unsigned long mask)
{
    return sizeof(mask) * 8 - __builtin_clzl(mask) - 1;
}

static inline int
fls(int mask)
{
    return (mask == 0 ? mask : (int)bsrl((unsigned int)mask) + 1);
}
#endif
