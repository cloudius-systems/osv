/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "safe-ptr.hh"

#include <osv/execinfo.hh>

struct frame {
    frame* next;
    void* pc;
};

int backtrace_safe(void** pc, int nr)
{
    frame* rbp;
    frame* next;

    asm("mov %%rbp, %0" : "=rm"(rbp));
    int i = 0;
    while (i < nr
            && safe_load(&rbp->next, next)
            && safe_load(&rbp->pc, pc[i])
            && pc[i]) {
        rbp = next;
        ++i;
    }
    return i;
}



