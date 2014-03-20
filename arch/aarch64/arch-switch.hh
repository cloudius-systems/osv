/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_SWITCH_HH_
#define ARCH_SWITCH_HH_

#include <osv/barrier.hh>
#include <string.h>

extern "C" {
void thread_main(void);
void thread_main_c(sched::thread* t);
void stack_trampoline(sched::thread* t, void (*func)(sched::thread*),
                      void** stacktop);
}

namespace sched {

void thread::switch_to()
{
    abort();
}

void thread::switch_to_first()
{
    abort();
}

void thread::init_stack()
{
    auto& stack = _attr._stack;
    if (!stack.size) {
        stack.size = 65536;
    }
    if (!stack.begin) {
        stack.begin = malloc(stack.size);
        stack.deleter = stack.default_deleter;
    }
    void** stacktop = reinterpret_cast<void**>(stack.begin + stack.size);
    _state.fp = this;
    _state.pc = reinterpret_cast<void*>(thread_main);
    _state.sp = stacktop;
}

void thread::setup_tcb()
{
    assert(tls.size);

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
    abort();
}

}

#endif /* ARCH_SWITCH_HH_ */
