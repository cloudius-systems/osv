/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * fork_thread() for aarch64 -- mirrors arch/x64/fork.cc.  fork() (fork.cc)
 * passes the caller's return address and stack pointer; we copy the parent's
 * user stack, bias the SP into the copy, and start a child thread that installs
 * the copied stack, sets x0=0 (fork()'s return value in the child), and returns
 * to fork()'s caller.
 */

#include "arch.hh"
#include <errno.h>
#include <string.h>
#include <cstdlib>
#include <osv/sched.hh>

// pthread_atfork child-handler chain (defined in libc/pthread.cc), run in the
// child's context before it resumes user code.
extern "C" void __osv_run_atfork_child();

sched::thread *fork_thread(void *caller_ret, void *caller_sp,
                           void *resume_ctx, void **out_stack_to_free)
{
    (void)resume_ctx;   // aarch64 uses caller_sp bias-copy (same-VA is x86-64)
    auto parent = sched::thread::current();
    auto parent_pinned_cpu = parent->pinned() ? sched::cpu::current() : nullptr;

    auto si = parent->get_stack_info();
    char *stack_base = static_cast<char*>(si.begin) + si.size;
    char *sp = static_cast<char*>(caller_sp);
    if (sp < static_cast<char*>(si.begin) || sp > stack_base) {
        return nullptr;
    }
    size_t stack_size = si.size;

    char *child_stack_mem = static_cast<char*>(malloc(stack_size));
    if (!child_stack_mem) {
        return nullptr;
    }
    // Copy ONLY the live top [caller_sp .. stack_base) into the top of the
    // child buffer (app stacks are demand-paged; copying from si.begin faults).
    char *child_base = child_stack_mem + stack_size;
    ptrdiff_t bias = child_base - stack_base;
    size_t live = static_cast<size_t>(stack_base - sp);
    memcpy(child_base - live, sp, live);
    char *child_sp = sp + bias;

    volatile u64 resume_sp = reinterpret_cast<u64>(child_sp);
    volatile u64 resume_pc = reinterpret_cast<u64>(caller_ret);
    char *stack_to_free = child_stack_mem;

    // TLS: the child is a real OSv thread with its own fresh setup_tcb() block.
    // Only override tpidr_el0 if the parent had installed its own app TCB via
    // arch_prctl (parent_app_tcb != 0); otherwise keep the child's private OSv
    // TLS (the clean case for a musl app built against OSv's libc).
    u64 parent_app_tcb = parent->get_app_tcb();
    auto t = sched::thread::make([resume_sp, resume_pc, parent_app_tcb] {
        if (parent_app_tcb) {
            asm volatile ("msr tpidr_el0, %0; isb" :: "r"(parent_app_tcb) : "memory");
        }
        // Run pthread_atfork child handlers in the child's context before
        // resuming user code.
        __osv_run_atfork_child();
        asm volatile
          ("mov sp, %0 \n\t"    // install the private copied stack
           "mov x0, #0 \n\t"    // fork() returns 0 in the child
           "br %1 \n\t"         // resume in fork()'s caller
           : : "r"(resume_sp), "r"(resume_pc) : "x0", "memory");
    }, sched::thread::attr().
        stack(4096 * 4).
        // Detached: nobody join()s the fork child (the parent reaps it via the
        // pid registry / waitpid, not sched::thread::join).  A detached thread
        // is handed to the reaper on completion, which runs our set_cleanup()
        // (freeing the copied stack and disposing the thread object, releasing
        // its application_runtime reference).  Without this the thread object
        // (and its app_runtime shared_ptr) would leak and OSv would hang at
        // shutdown -- see the cleanup comment in libc/process/fork.cc.
        detached(),
        false,
        true);
    t->set_app_tcb(parent->get_app_tcb());
    if (parent_pinned_cpu) {
        t->pin(parent_pinned_cpu);
    }
    if (out_stack_to_free) {
        *out_stack_to_free = stack_to_free;
    }
    return t;
}
