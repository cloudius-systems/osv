/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_TLS_HH
#define ARCH_TLS_HH

// Don't change the declaration sequence of all existing members'.
// Please add new members from the last.
struct thread_control_block {
    thread_control_block* self;
    void* tls_base;
    //
    // This field, a per-thread stack for SYSCALL instruction, is  used in
    // arch/x64/entry.S for %fs's offset.  We currently keep this field in the TCB
    // to make it easier to access in assembly code through a known offset at %fs:16.
    // But with more effort, we could have used an ordinary thread-local variable
    // instead and avoided extending the TCB here.
    //
    // The 8 bytes at the top of the syscall stack are used to identify if
    // the stack is tiny (0) or large (1). So the size of the syscall stack is in
    // reality smaller by 16 bytes from what was originally allocated because we need
    // to make it 16-bytes aligned.
    void* syscall_stack_top;
    //
    // This field is used to store the syscall caller stack pointer (value of RSP when
    // SYSCALL was called) so that it can be restored when syscall completed.
    // Same as above this field could be an ordinary thread-local variable.
    void* syscall_caller_stack_pointer;
};

#endif /* ARCH_TLS_HH */
