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

namespace sched {

struct arch_cpu;
struct arch_thread;

struct arch_cpu {
    arch_cpu();

    unsigned int gic_id;

    void init_on_cpu();
};

struct arch_thread {
};

struct arch_fpu {
    struct processor::fpu_state s;
    void save() { processor::fpu_state_save(&s); }
    void restore() { processor::fpu_state_load(&s); }
};

inline arch_cpu::arch_cpu()
{
}

inline void arch_cpu::init_on_cpu()
{
    processor::halt_no_interrupts();
}

}

#endif /* ARCH_CPU_HH */
