/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "safe-ptr.hh"
#include "exceptions.hh"

#include <osv/execinfo.hh>

struct frame {
    frame* next;
    void* pc;
};

// Walk a frame-pointer chain, appending return addresses to pc[].
static inline __attribute__((always_inline)) int unwind_fp_chain(frame* rbp, void** pc, int nr, int i)
{
    frame* next;

    while (i < nr
            && rbp
            && safe_load(&rbp->next, next)
            && safe_load(&rbp->pc, pc[i])
            && pc[i]) {
        rbp = next;
        ++i;
    }
    return i;
}

int backtrace_safe(void** pc, int nr)
{
    frame* rbp;

    asm("mov %%rbp, %0" : "=rm"(rbp));
    return unwind_fp_chain(rbp, pc, nr, 0);
}

int backtrace_safe_from_interrupt(void** pc, int nr)
{
    // current_interrupt_frame is set for the duration of interrupt() and is
    // null everywhere else, so this cleanly distinguishes "sampling an
    // interrupted thread" from "tracing my own call path".
    exception_frame* ef = current_interrupt_frame;
    if (!ef) {
        return backtrace_safe(pc, nr);
    }
    if (nr < 1) {
        return 0;
    }
    // The saved rip is the instruction that was executing when the interrupt
    // arrived.  It is the whole point of this function and it needs no
    // unwinding at all, so it is always correct even where the interrupted
    // code was built without frame pointers.
    // current_interrupt_frame is __thread and this is the first code to read it
    // from arbitrary tracepoint context, where fsbase may point at an app TCB.
    // A non-null garbage ef would fault on a plain load, so read it the way the
    // rest of this file reads untrusted memory.
    u64 rip = 0;
    if (!safe_load(&ef->rip, rip)) {
        return 0;
    }
    pc[0] = (void*)rip;
    // Continue up the interrupted thread's callers.  A frame-pointer walk can
    // only ever reach callers, never the leaf, which is why pc[0] has to come
    // from the frame above.
    u64 rbp = 0;
    if (!safe_load(&ef->rbp, rbp)) {
        return 1;
    }
    return unwind_fp_chain((frame*)rbp, pc, nr, 1);
}



