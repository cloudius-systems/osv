/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Byte ctype with conf_locale=1: __ctype_b_loc(), __ctype_toupper_loc(),
// __ctype_tolower_loc() (the tables glibc-built code such as libstdc++
// indexes directly), toupper(), tolower() and their _l forms follow the
// LC_CTYPE of the locale in use, as glibc's do. A loaded LC_CTYPE answers
// from its file's own 384-entry tables (index -128..255; the format is in
// the format is in locale-data.h); C and C.UTF-8 keep musl's tables, whose
// objects the Makefile builds under __osv_musl_* names for this file, and
// musl's ASCII case mapping (its toupper.o and tolower.o are not built).
// Measured on glibc 2.41: the class tables of every tested UTF-8 locale are
// C's; tr_TR's case tables map only ASCII and leave 'i' and 'I' unchanged.
// The table pointers are fields of the loaded data, which is never freed,
// so a pointer __ctype_b_loc() returned stays valid; like glibc's, it shows
// the locale current at the call.
// Known gap: musl's isalpha() and the other byte classifiers are
// arithmetic on ASCII and do not read __ctype_b_loc(), so for a locale whose
// byte classes are not C's they would disagree with it. Every measured UTF-8
// locale has C's byte classes (glibc 2.41), so they agree today.

#include <ctype.h>

#include <osv/export.h>

#include "libc/internal/locale_impl.h"
#include "libc/internal/libc.h"
#include "locale-data.h"

extern "C" {
const unsigned short **__osv_musl_ctype_b_loc(void);
const int32_t **__osv_musl_ctype_toupper_loc(void);
const int32_t **__osv_musl_ctype_tolower_loc(void);
}

namespace {

const osv_ctype *loaded(locale_t l)
{
    auto d = static_cast<const osv_locale_data *>(__osv_ctype_data(l));
    return d ? &d->ctype : nullptr;
}

int byte_map(int c, locale_t l, bool upper)
{
    const osv_ctype *t = loaded(l);
    if (!t) {
        // musl's: ASCII letters only (C and C.UTF-8 agree with glibc).
        if (upper) {
            return unsigned(c) - 'a' < 26 ? c & 0x5f : c;
        }
        return unsigned(c) - 'A' < 26 ? c | 32 : c;
    }
    if (c < -128 || c > 255) {
        return c;
    }
    return upper ? t->toupper_b[c] : t->tolower_b[c];
}

}

extern "C" OSV_LIBC_API
const unsigned short **__ctype_b_loc(void) noexcept
{
    const osv_ctype *t = loaded(nullptr);
    return t ? const_cast<const unsigned short **>(&t->class_b) : __osv_musl_ctype_b_loc();
}

extern "C" OSV_LIBC_API
const int32_t **__ctype_toupper_loc(void) noexcept
{
    const osv_ctype *t = loaded(nullptr);
    return t ? const_cast<const int32_t **>(&t->toupper_b) : __osv_musl_ctype_toupper_loc();
}

extern "C" OSV_LIBC_API
const int32_t **__ctype_tolower_loc(void) noexcept
{
    const osv_ctype *t = loaded(nullptr);
    return t ? const_cast<const int32_t **>(&t->tolower_b) : __osv_musl_ctype_tolower_loc();
}

extern "C" OSV_LIBC_API
int toupper(int c) noexcept
{
    return byte_map(c, nullptr, true);
}

extern "C" OSV_LIBC_API
int tolower(int c) noexcept
{
    return byte_map(c, nullptr, false);
}

extern "C" OSV_LIBC_API
int __toupper_l(int c, locale_t l) noexcept
{
    return byte_map(c, l, true);
}
weak_alias(__toupper_l, toupper_l);

extern "C" OSV_LIBC_API
int __tolower_l(int c, locale_t l) noexcept
{
    return byte_map(c, l, false);
}
weak_alias(__tolower_l, tolower_l);
