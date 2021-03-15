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
#include "arch-setup.hh"

extern "C" {
void thread_main_c(sched::thread* t);
}

namespace sched {

void thread::switch_to_first()
{
    asm volatile ("msr tpidr_el0, %0; isb; " :: "r"(_tcb) : "memory");

    /* check that the tls variable preempt_counter is correct */
    assert(sched::get_preempt_counter() == 1);

    s_current = this;
    current_cpu = _detached_state->_cpu;
    remote_thread_local_var(percpu_base) = _detached_state->_cpu->percpu_base;

    asm volatile("\n"
                 "ldp x29, x0, %2  \n"
                 "ldp x22, x21, %3 \n"
                 "mov sp, x22      \n"
                 "blr x21          \n"
                 : // No output operands - this is to designate the input operands as earlyclobbers
                   "=&Ump"(this->_state.fp), "=&Ump"(this->_state.sp)
                 : "Ump"(this->_state.fp), "Ump"(this->_state.sp)
                 : "x0", "x19", "x20", "x21", "x22", "x23", "x24",
                   "x25", "x26", "x27", "x28", "x30", "memory");
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
    _state.fp = 0;
    _state.thread = this;
    _state.sp = stacktop;
    _state.pc = reinterpret_cast<void*>(thread_main_c);
}

void thread::setup_tcb()
{   //
    // Most importantly this method allocates TLS memory region and
    // sets up TCB (Thread Control Block) that points to that allocated
    // memory region. The TLS memory region is designated to a specific thread
    // and holds thread local variables (with __thread modifier) defined
    // in OSv kernel and the application ELF objects including dependant ones
    // through DT_NEEDED tag.
    //
    // Each ELF object and OSv kernel gets its own TLS block with offsets
    // specified in DTV structure (the offsets get calculated as ELF is loaded and symbols
    // resolved before we get to this point).
    //
    // Because both OSv kernel and position-in-dependant (pie) or position-dependant
    // executable (non library) are compiled to use local-exec mode to access the thread
    // local variables, we need to setup the offsets and TLS blocks in a special way
    // to avoid any collisions. Specifically we define OSv TLS segment
    // (see arch/aarch64/loader.ld for specifics) with an extra buffer at
    // the beginning of the kernel TLS to accommodate TLS block of pies and
    // position-dependant executables.
    //
    // Please note that the TLS layout conforms to the variant I (1),
    // which means for example that all variable offsets are positive.
    // It also means that individual objects are laid out from the left to the right.

    // (1) - TLS memory area layout with app shared library
    // |------|--------------|-----|-----|-----|
    // |<NONE>|KERNEL        |SO_1 |SO_2 |SO_3 |
    // |------|--------------|-----|-----|-----|

    // (2) - TLS memory area layout with pie or
    // position dependant executable
    // |------|--------------|-----|-----|
    // | EXE  |KERNEL        |SO_2 |SO_3 |
    // |------|--------------|-----|-----|

    assert(tls.size);

    void* user_tls_data;
    size_t user_tls_size = 0;
    size_t executable_tls_size = 0;
    if (_app_runtime) {
        auto obj = _app_runtime->app.lib();
        assert(obj);
        user_tls_size = obj->initial_tls_size();
        user_tls_data = obj->initial_tls();
        if (obj->is_executable()) {
           executable_tls_size = obj->get_tls_size();
        }
    }

    // In arch/aarch64/loader.ld, the TLS template segment is aligned to 64
    // bytes, and that's what the objects placed in it assume. So make
    // sure our copy is allocated with the same 64-byte alignment, and
    // verify that object::init_static_tls() ensured that user_tls_size
    // also doesn't break this alignment.
    auto kernel_tls_size = sched::tls.size;
    assert(align_check(kernel_tls_size, (size_t)64));
    assert(align_check(user_tls_size, (size_t)64));

    auto total_tls_size = kernel_tls_size + user_tls_size;
    void* p = aligned_alloc(64, total_tls_size + sizeof(*_tcb));
    _tcb = (thread_control_block *)p;
    _tcb[0].tls_base = &_tcb[1];
    _state.tcb = p;
    //
    // First goes kernel TLS data
    auto kernel_tls = _tcb[0].tls_base;
    memcpy(kernel_tls, sched::tls.start, sched::tls.filesize);
    memset(kernel_tls + sched::tls.filesize, 0,
           kernel_tls_size - sched::tls.filesize);
    //
    // Next goes user TLS data
    if (user_tls_size) {
        memcpy(kernel_tls + kernel_tls_size, user_tls_data, user_tls_size);
    }

    if (executable_tls_size) {
        // If executable, then copy its TLS block data at the designated offset
        // at the beginning of the area as described in the ascii art for executables
        // TLS layout
        _app_runtime->app.lib()->copy_local_tls(kernel_tls);
    }
}

void thread::free_tcb()
{
    free(_tcb);
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
