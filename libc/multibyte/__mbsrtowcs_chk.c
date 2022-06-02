/*
 * Copyright (C) 2022 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <wchar.h>
#include <locale.h>
#include <libc/internal/libc.h>

size_t __mbsrtowcs_chk(wchar_t *dst, const char **src, size_t len, mbstate_t *ps, size_t dstlen)
{
    if (len > dstlen) {
        _chk_fail("mbsrtowcs");
    }
    return mbsrtowcs(dst, src, len, ps);
}
