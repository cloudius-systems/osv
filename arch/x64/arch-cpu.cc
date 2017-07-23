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
    if (exception_depth == nr_exception_stacks) {
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

extern "C"
[[gnu::target("no-sse")]]
void fpu_state_init_xsave(processor::fpu_state *s) {
    // s->xsavehdr is known at compile time, so gcc will not call memset()
    // here. Rather it will generate direct instructions to zero the given
    // length (24 bytes). We can't allow the compiler to use any SSE registers
    // for that, because this function is used in fpu_lock before saving the
    // FPU state to the stack, so must not touch any of the FPU registers.
    // This is why the "gnu::target(no-sse)" specification above is critical.
    memset(s->xsavehdr, 0, sizeof(s->xsavehdr));
}

extern "C"
void fpu_state_init_fxsave(processor::fpu_state *s) {
}

extern "C"
void fpu_state_save_xsave(processor::fpu_state *s) {
    processor::xsave(s, -1);
}

extern "C"
void fpu_state_save_fxsave(processor::fpu_state *s) {
    processor::fxsave(s);
}

extern "C"
void fpu_state_restore_xsave(processor::fpu_state *s) {
    processor::xrstor(s, -1);
}

extern "C"
void fpu_state_restore_fxsave(processor::fpu_state *s) {
    processor::fxrstor(s);
}

extern "C"
void (*resolve_fpu_state_init())(processor::fpu_state *s) {
    if (processor::features().xsave) {
        return fpu_state_init_xsave;
    } else {
        return fpu_state_init_fxsave;
    }
}
extern "C"
void (*resolve_fpu_state_save())(processor::fpu_state *s) {
    if (processor::features().xsave) {
        return fpu_state_save_xsave;
    } else {
        return fpu_state_save_fxsave;
    }
}

extern "C"
void (*resolve_fpu_state_restore())(processor::fpu_state *s) {
    if (processor::features().xsave) {
        return fpu_state_restore_xsave;
    } else {
        return fpu_state_restore_fxsave;
    }
}

void fpu_state_init(processor::fpu_state *s)
    __attribute__((ifunc("resolve_fpu_state_init")));
void fpu_state_save(processor::fpu_state *s)
    __attribute__((ifunc("resolve_fpu_state_save")));
void fpu_state_restore(processor::fpu_state *s)
    __attribute__((ifunc("resolve_fpu_state_restore")));

}
