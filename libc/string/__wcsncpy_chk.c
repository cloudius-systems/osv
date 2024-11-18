/*
 * Copyright (C) 2022 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <wchar.h>
#include <assert.h>

wchar_t *__wcsncpy_chk(wchar_t * dest, const wchar_t * src, size_t n, size_t destlen)
{
    assert(wcslen(src) + sizeof(L'\0') <= destlen);
    return wcsncpy(dest, src, n);
}
