/*
 * Copyright (C) 2022 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <wchar.h>
#include <libc/internal/libc.h>

size_t __mbstowcs_chk(wchar_t *restrict dest, const char *restrict src, size_t n, size_t dstlen)
{
    if (n > dstlen) {
        _chk_fail("mbstowcs");
    }
    return mbstowcs(dest, src, n);
}
