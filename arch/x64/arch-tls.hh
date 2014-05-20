/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_TLS_HH
#define ARCH_TLS_HH

struct thread_control_block {
    thread_control_block* self;
    void* tls_base;
};

#endif /* ARCH_TLS_HH */
