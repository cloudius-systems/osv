/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SYSCALL_HH_
#define SYSCALL_HH_

struct syscall_stack {
    //
    // This field, a per-thread stack for SYSCALL instruction, is  used in
    // arch/x64/entry.S for %gs's offset.  We currently keep this field in
    // the per-cpu structure to make it easier to access in assembly
    // code through a known offset at %gs:0.
    //
    // The 8 bytes at the top of the syscall stack are used to identify if
    // the stack is tiny (0) or large (1). So the size of the syscall stack is in
    // reality smaller by 16 bytes from what was originally allocated because we need
    // to make it 16-bytes aligned.
    void* stack_top;
    //
    // This field is used to store the syscall caller stack pointer (value of RSP when
    // SYSCALL was called) so that it can be restored when syscall completed.
    // Same as above this field could be an ordinary thread-local variable.
    void* caller_stack_pointer;
};


#endif
