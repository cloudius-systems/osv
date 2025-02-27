/*
 * Copyright (C) 2025 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

__thread int var_lib_tls;

extern "C"
void external_library(int N)
{
    for (register int i = 0; i < N; i++) {
        // To force gcc to not optimize this loop away
        asm volatile("" : : : "memory");
        ++var_lib_tls;
    }
}
