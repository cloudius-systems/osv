/*
 * Copyright (C) 2026 ScyllaDB
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A library, compiled with -mtls-dialect=gnu2, which accesses through a TLS
// descriptor a thread-local variable defined in libtls_gd.so.
extern __thread int gd1;

extern "C" void gd1_increment()
{
    gd1++;
}
