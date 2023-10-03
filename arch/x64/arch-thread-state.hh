/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_THREAD_STATE_HH_
#define ARCH_THREAD_STATE_HH_

#include "syscall.hh"

struct thread_state {
    char *exception_stack;
    void* rsp;
    void* rbp;
    void* rip;
    syscall_stack _syscall_stack;
};

#endif /* ARCH_THREAD_STATE_HH_ */
