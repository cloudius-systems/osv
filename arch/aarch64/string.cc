/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <bits/alltypes.h>
#include <osv/string.h>

extern "C"
void *memcpy_base(void *__restrict dest, const void *__restrict src, size_t n);
extern "C"
void *memset_base(void *__restrict dest, int c, size_t n);
extern "C"
void *memcpy_base_backwards(void *__restrict dest, const void *__restrict src, size_t n);


extern "C"
void *memcpy(void *__restrict dest, const void *__restrict src, size_t n)
{
    return memcpy_base(dest, src, n);
}

extern "C"
void *memcpy_backwards(void *__restrict dest, const void *__restrict src, size_t n)
{
    return memcpy_base_backwards(dest, src, n);
}

extern "C"
void *memset(void *__restrict dest, int c, size_t n)
{
    return memset_base(dest, c, n);
}
