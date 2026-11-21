/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_THREAD_STATE_HH_
#define ARCH_THREAD_STATE_HH_

#include <osv/kernel_config_fork.h>

struct thread_state {
    void* fp;            // offset 0  (used by arch/aarch64/sched.S)
    void* thread;        // offset 8

    void* sp;            // offset 16 (sched.S)
    void* pc;            // offset 24 (sched.S)
    void* tcb;           // offset 32 (sched.S)

    void* exception_sp;  // offset 40 SP_EL0 (sched.S)
    u64 stack_selector;  // offset 48  1 - SP_ELx (default), 0 - SP_EL0 (exceptions) (sched.S)
#if CONF_fork
    // offset 56.  Fork COW address-space reload: the TTBR0_EL1 value the context
    // switch must load when switching INTO this thread (its private COW root for
    // a fork child, AS0's root when returning to the kernel/non-fork AS), or 0
    // when no address-space change is needed (the common single-AS case, which
    // then pays nothing).  Set by cpu::schedule_next_thread() (core/sched.cc) and
    // consumed by reschedule_from_interrupt (arch/aarch64/sched.S) at the stack
    // swap -- the aarch64 analogue of x86 reloading CR3 at the rsp/rbp swap.
    u64 reload_ttbr0;    // offset 56
#endif
};

#endif /* ARCH_THREAD_STATE_HH_ */
