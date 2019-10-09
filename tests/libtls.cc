/*
 * Copyright (C) 2015 Paweł Dziepak
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// ex2 and ex3 have different attributes defined here than in tst-tls.cc
// That's on purpose, no matter what TLS access model is used in the end
// the same part of memory should be accessed.
__thread int ex1 = 321;
__thread int ex2 __attribute__ ((tls_model ("initial-exec"))) = 432;
__thread int ex3 = 765;

extern __thread int v1;
extern __thread int v5;

void external_library()
{
    // ex1 and ex3 get accessed by _tls_get_addr()
    ex1++;
    ex2++;
    ex3++;
    // These 2 below get handled by get _tls_get_addr() function in core/elf.cc
    v1++;
    v5++;
}
