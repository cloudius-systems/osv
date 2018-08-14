/*
 * Copyright (C) 2018 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <wchar.h>
#include <assert.h>

wchar_t *__wcscpy_chk(wchar_t *__restrict dest, const wchar_t *__restrict src, size_t destlen)
{
    assert(wcslen(src) + sizeof(L'\0') <= destlen);
    return wcscpy(dest, src);
}
