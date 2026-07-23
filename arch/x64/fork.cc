/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * fork_thread(): create a child thread that resumes in fork()'s CALLER on the
 * SAME user stack virtual addresses the parent had -- no stack relocation, no
 * SP bias -- and with the caller's FULL callee-saved register context restored,
 * exactly as a normal `ret` from fork() would leave it.  The child's address
 * space (built by clone_address_space) maps the parent's live stack VA range to
 * freshly-allocated PRIVATE physical pages that byte-copy the parent's stack,
 * so the child sees identical stack contents at identical addresses but writes
 * to its own private frames.
 *
 * Why same-VA + full register restore: OSv/x86-64 code addresses saved frame
 * pointers (rbp), return addresses, and &local pointers as absolute stack VAs,
 * and keeps live values in callee-saved registers (rbx, r12-r15) across calls.
 * Relocating the child stack to a different VA and biasing rsp leaves every
 * saved rbp/&local pointing at the PARENT's stack (off by the bias), and simply
 * jmp-ing to the return address skips fork()'s epilogue so the caller resumes
 * with fork()'s internal register values.  Either corrupts a deep unwind.
 * Keeping the child on the parent's exact VAs (private phys) AND restoring the
 * full caller register context (osv::fork_resume_ctx, captured at fork() entry)
 * makes the child continue in fork()'s caller as if fork() had returned 0 -- the
 * only correct way to fork a deep stack (see tst-fork-deep, documentation/fork.md).
 */

#include "arch.hh"
#include "tls-switch.hh"
#include <errno.h>
#include <string.h>
#include <cstdlib>
#include <osv/sched.hh>
#include <osv/fork.hh>
#include <osv/debug.hh>

// pthread_atfork child-handler chain (defined in libc/pthread.cc), run in the
// child's context before it resumes user code.
extern "C" void __osv_run_atfork_child();

sched::thread *fork_thread(void *caller_ret, void *caller_sp,
                           void *resume_ctx, void **out_stack_to_free)
{
    auto ctx = static_cast<osv::fork_resume_ctx*>(resume_ctx);
    auto parent = sched::thread::current();
    auto parent_pinned_cpu = parent->pinned() ? sched::cpu::current() : nullptr;

    auto si = parent->get_stack_info();
    char *stack_base = static_cast<char*>(si.begin) + si.size;
    char *sp = static_cast<char*>(caller_sp);
    if (sp < static_cast<char*>(si.begin) || sp > stack_base) {
        return nullptr;   // caller SP not within the known user stack
    }

    // Same-VA stack: the child resumes on the parent's EXACT register context.
    // No copy and no bias here -- clone_address_space() privatizes the parent's
    // live stack VA range into the child's address space (fresh private pages
    // that byte-copy the parent's stack), so these very addresses are valid and
    // private in the child.  Capture the resume context by value in the lambda
    // (the parent's on-stack ctx is gone by the time the child runs).
    (void)caller_ret;
    osv::fork_resume_ctx rc = *ctx;

    // TLS handling.  The child is a real OSv sched::thread, so its constructor
    // already ran setup_tcb() and installed a FRESH, private OSv TLS block.  If
    // the parent installed its own app TCB via arch_prctl (app_tcb != 0), the
    // child inherits it (shared) -- the documented multi-process-glibc limit;
    // otherwise the child keeps its own private OSv per-thread TLS.
    u64 parent_app_tcb = parent->get_app_tcb();

    auto t = sched::thread::make([rc, parent_app_tcb] {
        if (parent_app_tcb) {
            arch::set_fsbase(parent_app_tcb);
        }
        // Run pthread_atfork child handlers in the child's context (e.g. reset
        // the malloc arena lock) before resuming user code.
        __osv_run_atfork_child();
        // Restore the caller's full callee-saved register context and resume in
        // fork()'s caller with return value 0, on the parent's exact stack VAs
        // (private phys in the child AS).  We base all loads off a scratch
        // register (rax) pointing at a LOCAL copy of the context, and restore
        // the base-conflicting registers (rbx, rbp) LAST, so the load sequence
        // never clobbers its own base pointer.
        osv::fork_resume_ctx c = rc;   // local copy the asm can address stably
        // %0 = &c pinned in a register that is NOT one of the restored regs
        // (we let the compiler pick, but we consume it only to seed rax).  We
        // move &c into rax first, then load every callee-saved reg from rax-
        // relative offsets, and load rsp last -- rax (the base) is never in the
        // restore set, so the sequence never clobbers its own base pointer.
        // Offsets match struct fork_resume_ctx { rbx,rbp,r12,r13,r14,r15,rsp,rip }.
        asm volatile
          ("movq %0, %%rax        \n\t"  // rax = &c (base; not restored)
           "movq  0(%%rax), %%rbx \n\t"  // rbx
           "movq  8(%%rax), %%rbp \n\t"  // rbp
           "movq 16(%%rax), %%r12 \n\t"  // r12
           "movq 24(%%rax), %%r13 \n\t"  // r13
           "movq 32(%%rax), %%r14 \n\t"  // r14
           "movq 40(%%rax), %%r15 \n\t"  // r15
           "movq 56(%%rax), %%rcx \n\t"  // rcx = caller rip (scratch)
           "movq 48(%%rax), %%rsp \n\t"  // adopt parent's exact rsp
           "xorq %%rax, %%rax     \n\t"  // fork() returns 0 in the child
           "jmpq *%%rcx           \n\t"  // resume in fork()'s caller
           : : "r"(&c) : "rax", "rcx", "memory");
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
    // Same-VA: no separate user-stack buffer to free (the child's stack pages
    // are owned by its address space and freed on destroy_address_space()).
    if (out_stack_to_free) {
        *out_stack_to_free = nullptr;
    }
    return t;
}
