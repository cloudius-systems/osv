/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_SWITCH_HH_
#define ARCH_SWITCH_HH_

#include "msr.hh"
#include "barrier.hh"
#include <string.h>

extern "C" {
void thread_main(void);
void thread_main_c(sched::thread* t);
void stack_trampoline(sched::thread* t, void (*func)(sched::thread*),
                      void** stacktop);
}

namespace sched {

void set_fsbase_msr(u64 v)
{
    processor::wrmsr(msr::IA32_FS_BASE, v);
}

void set_fsbase_fsgsbase(u64 v)
{
    processor::wrfsbase(v);
}

extern "C"
void (*resolve_set_fsbase(void))(u64 v)
{
    // can't use processor::features, because it is not initialized
    // early enough.
    if (processor::features().fsgsbase) {
        return set_fsbase_fsgsbase;
    } else {
        return set_fsbase_msr;
    }
}

void set_fsbase(u64 v) __attribute__((ifunc("resolve_set_fsbase")));

void thread::switch_to()
{
    thread* old = current();
    // writing to fs_base invalidates memory accesses, so surround with
    // barriers
    barrier();
    set_fsbase(reinterpret_cast<u64>(_tcb));
    barrier();
    _cpu->arch.set_interrupt_stack(&_arch);
    _cpu->arch.set_exception_stack(&_arch);
    asm volatile
        ("mov %%rbp, %c[rbp](%0) \n\t"
         "movq $1f, %c[rip](%0) \n\t"
         "mov %%rsp, %c[rsp](%0) \n\t"
         "mov %c[rsp](%1), %%rsp \n\t"
         "mov %c[rbp](%1), %%rbp \n\t"
         "jmpq *%c[rip](%1) \n\t"
         "1: \n\t"
         :
         : "a"(&old->_state), "c"(&this->_state),
           [rsp]"i"(offsetof(thread_state, rsp)),
           [rbp]"i"(offsetof(thread_state, rbp)),
           [rip]"i"(offsetof(thread_state, rip))
         : "rbx", "rdx", "rsi", "rdi", "r8", "r9",
           "r10", "r11", "r12", "r13", "r14", "r15", "memory");
}

void thread::switch_to_first()
{
    barrier();
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<u64>(_tcb));
    barrier();
    s_current = this;
    current_cpu = _cpu;
    remote_thread_local_var(percpu_base) = _cpu->percpu_base;
    _cpu->arch.set_interrupt_stack(&_arch);
    _cpu->arch.set_exception_stack(&_arch);
    asm volatile
        ("mov %c[rsp](%0), %%rsp \n\t"
         "mov %c[rbp](%0), %%rbp \n\t"
         "jmp *%c[rip](%0)"
         :
         : "c"(&this->_state),
           [rsp]"i"(offsetof(thread_state, rsp)),
           [rbp]"i"(offsetof(thread_state, rbp)),
           [rip]"i"(offsetof(thread_state, rip))
         : "rbx", "rdx", "rsi", "rdi", "r8", "r9",
           "r10", "r11", "r12", "r13", "r14", "r15", "memory");
}

void thread::init_stack()
{
    auto& stack = _attr.stack;
    if (!stack.size) {
        stack.size = 65536;
    }
    if (!stack.begin) {
        stack.begin = malloc(stack.size);
        stack.deleter = stack.default_deleter;
    }
    void** stacktop = reinterpret_cast<void**>(stack.begin + stack.size);
    _state.rbp = this;
    _state.rip = reinterpret_cast<void*>(thread_main);
    _state.rsp = stacktop;
}

void thread::on_thread_stack(thread* t)
{
    t->_func();
    t->complete();
}

void thread::setup_tcb()
{
    assert(tls.size);
    // FIXME: respect alignment
    void* p = malloc(sched::tls.size + sizeof(*_tcb));
    memcpy(p, sched::tls.start, sched::tls.size);
    _tcb = static_cast<thread_control_block*>(p + tls.size);
    _tcb->self = _tcb;
    _tcb->tls_base = p;
}

void thread::free_tcb()
{
    free(_tcb->tls_base);
}

void thread_main_c(thread* t)
{
    arch::irq_enable();
#ifdef CONF_preempt
    preempt_enable();
#endif
    t->main();
    t->complete();
}

}

#endif /* ARCH_SWITCH_HH_ */
