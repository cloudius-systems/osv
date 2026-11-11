/*
 * Copyright (C) 2026 ScyllaDB
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Like libtls.cc, but the thread-local variables ex1 and ex3 are not defined
// here: they are defined in libtls.cc, which this library is linked with.
// When tst-tls.cc dlopen()s this library, libtls.cc is loaded as its
// DT_NEEDED dependency, so this tests access to thread-local variables
// defined in another dlopen()-ed object.
extern __thread int ex1;
extern __thread int ex3;

extern __thread int v1;
extern __thread int v5;

extern "C"
void external_library()
{
    ex1++;
    ex3++;
    v1++;
    v5++;
}
