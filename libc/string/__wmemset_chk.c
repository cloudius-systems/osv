/*
 * Copyright (C) 2022 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <wchar.h>
#include <libc/internal/libc.h>

wchar_t *__wmemset_chk(wchar_t *dest, wchar_t c, size_t n, size_t destlen)
{
    if (n > destlen) {
        _chk_fail("wmemset");
    }
    return wmemset(dest, c, n);
}
