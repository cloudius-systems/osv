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
    void *tls_base; /* dtv */
    void *privat;   /* private */
};

#endif /* ARCH_TLS_HH */
