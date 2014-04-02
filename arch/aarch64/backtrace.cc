/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "safe-ptr.hh"
#include <osv/debug.h>

struct frame {
    frame* next;
    void* pc;
};

int backtrace_safe(void** pc, int nr)
{
    register frame* fp;
    frame* next;

    asm("mov %0, x29" : "=r"(fp));

    int i = 0;
    while (i < nr
           && safe_load(&fp->next, next)
           && safe_load(&fp->pc, pc[i])
           && pc[i]) {
        fp = next;
        ++i;
    }

    return i;
}
