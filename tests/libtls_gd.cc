/*
 * Copyright (C) 2026 ScyllaDB
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A library with a thread-local variable which it accesses only through
// __tls_get_addr() ("General Dynamic" model, -mtls-dialect=gnu), so OSv
// does not give this library a static TLS block. tst-tls-dlopen-gd.cc
// links with this library, and also dlopen()s libtls_gd_user.so, which
// accesses the same variable through a TLS descriptor.
__thread int gd1 = 111;

extern "C" int gd1_get()
{
    return gd1;
}
