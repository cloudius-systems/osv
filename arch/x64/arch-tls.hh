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
    unsigned long app_tcb;
    long kernel_tcb_counter; //If >=1 indicates that FS points to kernel TCB
};

#endif /* ARCH_TLS_HH */
