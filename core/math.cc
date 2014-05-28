/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <math.h>
#include <osv/types.h>

extern "C"
int __isnan(double v)
{
    u64 r;
    asm("cmpunordsd %1, %1; movq %1, %0" : "=rm"(r), "+x"(v));
    return r & 1;
}
