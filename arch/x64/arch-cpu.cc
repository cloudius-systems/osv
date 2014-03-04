/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch-cpu.hh"
#include <osv/sched.hh>
#include <osv/debug.hh>

namespace sched {

__thread unsigned exception_depth = 0;

inline void arch_cpu::enter_exception()
{
    if (exception_depth == nr_exception_stacks - 1) {
        abort("exception nested too deeply");
    }
    auto& s = percpu_exception_stack[exception_depth++];
    set_exception_stack(s, sizeof(s));
}

inline void arch_cpu::exit_exception()
{
    if (--exception_depth == 0) {
        set_exception_stack(&thread::current()->_arch);
    } else {
        auto& s = percpu_exception_stack[exception_depth - 1];
        set_exception_stack(s, sizeof(s));
    }
}

exception_guard::exception_guard()
{
    sched::cpu::current()->arch.enter_exception();
}

exception_guard::~exception_guard()
{
    sched::cpu::current()->arch.exit_exception();
}

}
