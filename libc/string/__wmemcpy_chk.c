/*
 * Copyright (C) 2022 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <wchar.h>
#include <libc/internal/libc.h>

wchar_t * __wmemcpy_chk(wchar_t *restrict dest, const wchar_t *restrict src, size_t len, size_t destlen)
{
    if (len > destlen) {
        _chk_fail("wmemcpy");
    }
    return wmemcpy(dest, src, len);
}
