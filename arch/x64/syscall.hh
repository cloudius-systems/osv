/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SYSCALL_HH_
#define SYSCALL_HH_

//This structure "describes" a per-thread syscall stack used when handling
//the SYSCALL instruction. There are 2 places it is used:
// - thread_state - holds information about this thread syscall stack
// - arch_cpu - holds information about this cpu CURRENT thread syscall stack
struct syscall_stack_descriptor {
    // The address of the top of the syscall stack.
    //
    // The 8 bytes at the top of the stack are used to identify if the stack
    // is tiny (0) or large (1). Therefore the size of the syscall stack is
    // in reality smaller by 16 bytes from what was originally allocated because we need
    // to make it 16-bytes aligned.
    void* stack_top;
    //
    // This field is used to store the syscall caller stack pointer (value of RSP when
    // SYSCALL was called) so that it can be restored when syscall completes.
    void* caller_stack_pointer;
};


#endif
