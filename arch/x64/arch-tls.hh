/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_TLS_HH
#define ARCH_TLS_HH

#include <stdint.h>
#include <osv/export.h>

// Don't change the declaration sequence of all existing members'.
// Please add new members from the last.
//
// The x86-64 psABI fixes the -fstack-protector canary at %fs:0x28 and the
// pointer guard at %fs:0x30; glibc's tcbhead_t and musl's struct pthread agree
// on both.  GCC and Clang hardcode those offsets into every stack-protected
// function's prologue and epilogue, so a stack-protected application (the
// distro default) reads %fs:0x28 directly, with no way to ask libc where the
// canary lives.
//
// This block must therefore be at least 0x38 bytes AND have the canary at
// exactly 0x28.  Before that was true the struct was 3 words (0x18) and was
// placed at the very END of the TLS allocation (see thread::setup_tcb() in
// arch-switch.hh), so every application canary read was an out-of-bounds read
// 16 bytes past the end of that buffer, returning whatever the allocator
// happened to have there.  reserved0/reserved1 only pad the ABI-mandated gap
// (glibc keeps multiple_threads/gscope_flag/sysinfo there); nothing on OSv
// reads them, and they exist solely to place stack_guard at 0x28.
struct thread_control_block {
    thread_control_block* self;   // 0x00
    void* tls_base;               // 0x08
    unsigned long app_tcb;        // 0x10
    unsigned long reserved0;      // 0x18 -- ABI padding, unused by OSv
    unsigned long reserved1;      // 0x20 -- ABI padding, unused by OSv
    uintptr_t stack_guard;        // 0x28 -- -fstack-protector canary (psABI)
    uintptr_t pointer_guard;      // 0x30 -- pointer-mangling guard (psABI)
};

static_assert(__builtin_offsetof(thread_control_block, stack_guard) == 0x28,
              "x86-64 psABI requires the stack-protector canary at %fs:0x28");
static_assert(__builtin_offsetof(thread_control_block, pointer_guard) == 0x30,
              "x86-64 psABI requires the pointer guard at %fs:0x30");

// The single process-wide guard value installed in every TCB.
//
// It MUST be process-wide, not per-thread.  A fork() child resumes mid-frame on
// the parent's stack (arch/x64/fork.cc), so every frame that was live across
// fork() carries the canary the PARENT spilled, while the child's epilogue
// re-reads the canary through its OWN fresh TCB.  Per-thread canaries would make
// those epilogues mismatch on every single fork -- turning today's intermittent
// abort into a deterministic one.  The same argument applies to pointer_guard: a
// pointer mangled before fork() is demangled after it.
//
// This is a CORRECTNESS fix, not a hardening feature: a fixed compile-time
// constant is trivially guessable and provides no real stack-protector security.
// It only guarantees that a canary check compares equal values.  We reuse the
// existing exported __stack_chk_guard (core/runtime.cc) so the kernel symbol and
// the TCB slot can never disagree.  Note the zero top byte, which is deliberate
// and matches glibc/musl: on little-endian it terminates the canary for str*
// overflows.
//
// ponytail: fixed constant, no entropy.  Upgrade path is to seed
// __stack_chk_guard once from the random device after it is up (setup_tcb() runs
// for the first kernel threads long before that), keeping it process-wide.
//
// Declared with C++ linkage to match its definition in core/runtime.cc, which
// includes this header via osv/sched.hh; a global-scope variable is unmangled
// either way, so this is the same symbol applications resolve.  OSV_LIBC_API
// keeps the declaration's visibility identical to the definition's under
// -fvisibility=hidden.
extern OSV_LIBC_API void* __stack_chk_guard;

inline uintptr_t tcb_guard_value()
{
    return reinterpret_cast<uintptr_t>(__stack_chk_guard);
}

#endif /* ARCH_TLS_HH */
