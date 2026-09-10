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
// The TCB sits at the end of the TLS allocation (thread::setup_tcb() in
// arch-switch.hh), so both slots must be inside this struct.  dtv sits at
// 0x18, where tlsdesc.S reads it as %fs:24.  reserved1 only pads the rest of
// the ABI-mandated gap (glibc keeps multiple_threads/gscope_flag/sysinfo
// there); nothing on OSv reads it, and it exists solely to place stack_guard
// at 0x28.
struct thread_control_block {
    thread_control_block* self;   // 0x00
    void* tls_base;               // 0x08
    unsigned long app_tcb;        // 0x10
    void* dtv;                    // 0x18: the DTV, read by __tlsdesc_dynamic (tlsdesc.S)
    unsigned long reserved1;      // 0x20: ABI padding, unused by OSv
    uintptr_t stack_guard;        // 0x28: -fstack-protector canary (psABI)
    uintptr_t pointer_guard;      // 0x30: pointer-mangling guard (psABI)
};

static_assert(__builtin_offsetof(thread_control_block, stack_guard) == 0x28,
              "x86-64 psABI requires the stack-protector canary at %fs:0x28");
static_assert(__builtin_offsetof(thread_control_block, pointer_guard) == 0x30,
              "x86-64 psABI requires the pointer guard at %fs:0x30");

// The single process-wide guard value installed in every TCB.
//
// It is process-wide, not per-thread, as on Linux: a context created on one
// thread can be resumed on another (ucontext-based fibers, coroutine libraries
// that move work between threads), and a stack-protected frame's epilogue then
// compares the canary the first thread spilled against the second thread's.
// The same holds for pointer_guard: a pointer mangled on one thread may be
// demangled on another.
//
// This guarantees only that a canary check compares equal values.  A fixed
// compile-time constant is trivially guessable and is no stack-protector
// hardening.  Reusing the exported __stack_chk_guard (runtime.cc) keeps the
// kernel symbol and the TCB slots from disagreeing.
//
// Seeding __stack_chk_guard from the random device would add the hardening.
// The new value must then be in place before the first stack-protected frame
// is live: each TCB copies the value when it is set up (setup_tcb(),
// arch_setup_tls()), so a TCB created before the seeding would keep the old
// constant, and a frame that spans the change would abort.
//
// Declared with C++ linkage to match its definition in runtime.cc, which
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
