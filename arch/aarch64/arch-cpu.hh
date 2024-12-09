/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_CPU_HH
#define ARCH_CPU_HH

#include "processor.hh"
#include "osv/pagealloc.hh"
#include <osv/debug.h>
#include "exceptions.hh"
#include <osv/kernel_config_threads_default_exception_stack_size.h>

struct init_stack {
    char stack[4096] __attribute__((aligned(16)));
    init_stack* next;
} __attribute__((packed));

namespace sched {

struct arch_cpu;
struct arch_thread;

struct arch_cpu {
    init_stack initstack;
    void init_on_cpu();
    int smp_idx; /* index into the cpus array */
    u64 mpid;    /* actual MPID as read from the cpu */
};

struct arch_thread {
    char exception_stack[CONF_threads_default_exception_stack_size] __attribute__((aligned(16)));
};

struct arch_fpu {
    struct processor::fpu_state s;
    void save() { fpu_state_save(&s); }
    void restore() { fpu_state_load(&s); }
};

// lock adapter for arch_fpu
class fpu_lock {
    arch_fpu _state;
public:
    void lock() { _state.save(); }
    void unlock() { _state.restore(); }
};

inline void arch_cpu::init_on_cpu()
{
    if (this->smp_idx != 0) {
        gic::gic->init_on_secondary_cpu(this->smp_idx);
    }
}

} /* namespace sched */

#endif /* ARCH_CPU_HH */
