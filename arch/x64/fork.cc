/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * fork_thread(): create a child thread that resumes in fork()'s CALLER, on a
 * private copy of the parent's user stack, returning 0 from fork() in the child.
 * The x86-64 arch half of the fork() emulation (see documentation/fork.md).
 *
 * fork() (libc/process/fork.cc) passes us the caller's resume point:
 *   caller_ret  = the address fork() would return to  (__builtin_return_address)
 *   caller_sp   = the parent's SP at fork()'s return   (fork()'s frame base)
 * We copy the parent stack region [caller_sp .. stack_base) into a fresh stack,
 * bias caller_sp into the copy, and start a child thread whose trampoline sets
 * rsp=child_sp, rax=0, and jumps to caller_ret -- i.e. the child returns from
 * fork() with value 0 on its own private stack, in the caller.
 */

#include "arch.hh"
#include "tls-switch.hh"
#include <errno.h>
#include <string.h>
#include <cstdlib>
#include <osv/sched.hh>

// pthread_atfork child-handler chain (defined in libc/pthread.cc), run in the
// child's context before it resumes user code.
extern "C" void __osv_run_atfork_child();

sched::thread *fork_thread(void *caller_ret, void *caller_sp, void **out_stack_to_free)
{
    auto parent = sched::thread::current();
    auto parent_pinned_cpu = parent->pinned() ? sched::cpu::current() : nullptr;

    auto si = parent->get_stack_info();
    char *stack_base = static_cast<char*>(si.begin) + si.size;
    char *sp = static_cast<char*>(caller_sp);
    if (sp < static_cast<char*>(si.begin) || sp > stack_base) {
        return nullptr;   // caller SP not within the known user stack
    }
    size_t stack_size = si.size;

    char *child_stack_mem = static_cast<char*>(malloc(stack_size));
    if (!child_stack_mem) {
        return nullptr;
    }
    // Copy ONLY the live top of the stack, [caller_sp .. stack_base), into the
    // TOP of the child buffer.  App (pthread) stacks are demand-paged: only the
    // used top is mapped, so copying from si.begin would fault on the first
    // unmapped page.  Keeping the copy at the top of the child buffer preserves
    // the base-relative bias so a biased SP resolves correctly.
    char *child_base = child_stack_mem + stack_size;
    ptrdiff_t bias = child_base - stack_base;
    size_t live = static_cast<size_t>(stack_base - sp);
    memcpy(child_base - live, sp, live);
    char *child_sp = sp + bias;

    volatile u64 resume_sp = reinterpret_cast<u64>(child_sp);
    volatile u64 resume_pc = reinterpret_cast<u64>(caller_ret);
    char *stack_to_free = child_stack_mem;

    // TLS handling.  The child is a real OSv sched::thread, so its constructor
    // already ran setup_tcb() and installed a FRESH, private OSv TLS block
    // (with its own errno and all libc __thread state).  Two cases:
    //
    //  (1) The app uses OSv's libc TLS (the normal dynamically-linked path,
    //      app_tcb == 0): the child's own fresh TCB is exactly right -- do NOT
    //      touch fsbase, let the child run on its private per-thread TLS.  This
    //      is the clean case and fork() "just works" for TLS.
    //  (2) The app installed its own TCB via arch_prctl(SET_FS) (app_tcb != 0,
    //      e.g. a glibc binary's __libc_setup_tls): the child would need a
    //      private COPY of that app TCB.  We do not duplicate it here yet;
    //      the child inherits the parent's app_tcb (shared), which is the
    //      documented multi-process-glibc limitation.  A musl app built against
    //      OSv's libc takes path (1) and avoids this entirely.
    u64 parent_app_tcb = parent->get_app_tcb();

    auto t = sched::thread::make([resume_sp, resume_pc, parent_app_tcb] {
        // Only override the child's own (fresh) TLS if the parent had installed
        // an app TCB via arch_prctl; otherwise keep the child's private OSv TCB.
        if (parent_app_tcb) {
            arch::set_fsbase(parent_app_tcb);
        }
        // Run pthread_atfork child handlers in the child's context (e.g. reset
        // the malloc arena lock) before resuming user code.
        __osv_run_atfork_child();
        asm volatile
          ("movq %0, %%rsp \n\t"    // install the private copied stack
           "xorq %%rax, %%rax \n\t" // fork() returns 0 in the child
           "jmpq *%1 \n\t"          // resume in fork()'s caller
           : : "r"(resume_sp), "r"(resume_pc) : "memory");
    }, sched::thread::attr().
        stack(4096 * 4),
        false,
        true);
    t->set_app_tcb(parent->get_app_tcb());
    if (parent_pinned_cpu) {
        t->pin(parent_pinned_cpu);
    }
    // The caller (fork.cc) owns the single cleanup; hand back the copied user
    // stack so it can be freed when the child is reaped.
    if (out_stack_to_free) {
        *out_stack_to_free = stack_to_free;
    }
    return t;
}
