/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_TLS_HH
#define ARCH_TLS_HH

/* Thread pointer points to the element containing the dtv pointer.
 * This is trying to match the data structures in
 * glibc-2.19/ports/sysdeps/aarch64/nptl/tls.h
 */

/* the structure (roughly) corresponds to the tcbhead_t in glibc */
struct thread_control_block {
    void *tls_base; // Address of the per-thread static TLS block (kernel, pie, etc)
    void *dtv;      // Address of the DTV (Dynamic Thread Vector)
};

// NOTE: the aarch64 TLS variant-I ABI reserves exactly two words (16 bytes) for
// the thread_control_block before the thread's TLS block, and the compiler
// emits every __thread access as tpidr_el0 + 16 + offset.  Do NOT add fields
// here: growing the TCB shifts every kernel TLS variable (e.g. preempt_counter)
// and breaks boot (assert in arch_setup_tls).  The app TLS base saved/restored
// across fork() lives in thread::_arch (arch_thread::app_tcb) instead.

#endif /* ARCH_TLS_HH */
