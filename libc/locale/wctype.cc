/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Wide-character classification, case mapping and width with glibc's
// behaviour in OSv's two locales:
//
// C: below 0x80 the C.UTF-8 answer (glibc's C and C.UTF-8 agree on ASCII);
// any other value has no class, maps to itself and has width -1.
// C.UTF-8: glibc's tables (ctype-utf8.cc).
//
// As in glibc, wctype() and wctrans() return descriptors bound to the
// LC_CTYPE current at the call (bit 0x100 marks UTF-8), and iswctype_l()
// and towctrans_l() ignore their locale argument.

#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <osv/export.h>

#include "libc/internal/locale_impl.h"
#include "libc/internal/libc.h"

extern "C" OSV_HIDDEN int __osv_utf8_iswctype(wint_t wc, int cls);
extern "C" OSV_HIDDEN wint_t __osv_utf8_towupper(wint_t wc);
extern "C" OSV_HIDDEN wint_t __osv_utf8_towlower(wint_t wc);
extern "C" OSV_HIDDEN int __osv_utf8_wcwidth(wchar_t wc);

namespace {

enum { ALNUM = 1, ALPHA, BLANK, CNTRL, DIGIT, GRAPH, LOWER, PRINT, PUNCT,
       SPACE, UPPER, XDIGIT };
constexpr unsigned long utf8_bit = 0x100;
enum { TOUPPER = 1, TOLOWER = 2 };

int classify(wint_t wc, int cls, bool utf8)
{
    if (!utf8 && wc >= 0x80) {
        return 0;
    }
    return __osv_utf8_iswctype(wc, cls);
}

wint_t map(wint_t wc, int how, bool utf8)
{
    if (!utf8 && wc >= 0x80) {
        return wc;
    }
    return how == TOUPPER ? __osv_utf8_towupper(wc) : __osv_utf8_towlower(wc);
}

unsigned long make_wctype(const char *s, bool utf8)
{
    static const char names[] =
        "alnum\0" "alpha\0" "blank\0" "cntrl\0" "digit\0" "graph\0"
        "lower\0" "print\0" "punct\0" "space\0" "upper\0" "xdigit";
    int i = 1;
    for (const char *p = names; *p; i++, p += 6) {
        if (!strcmp(s, p)) {
            return i | (utf8 ? utf8_bit : 0);
        }
    }
    return 0;
}

wctrans_t make_wctrans(const char *s, bool utf8)
{
    unsigned long how = !strcmp(s, "toupper") ? TOUPPER :
                        !strcmp(s, "tolower") ? TOLOWER : 0;
    return how ? reinterpret_cast<wctrans_t>(how | (utf8 ? utf8_bit : 0)) : nullptr;
}

}

#define WCLASS(name, cls)                                                    \
extern "C" OSV_LIBC_API int isw##name(wint_t wc) noexcept                    \
{                                                                            \
    return classify(wc, cls, OSV_CUR_UTF8);                                  \
}                                                                            \
extern "C" OSV_LIBC_API int __isw##name##_l(wint_t wc, locale_t l) noexcept  \
{                                                                            \
    return classify(wc, cls, __osv_ctype_utf8(l));                           \
}                                                                            \
weak_alias(__isw##name##_l, isw##name##_l);

WCLASS(alnum, ALNUM)
WCLASS(alpha, ALPHA)
WCLASS(blank, BLANK)
WCLASS(cntrl, CNTRL)
WCLASS(digit, DIGIT)
WCLASS(graph, GRAPH)
WCLASS(lower, LOWER)
WCLASS(print, PRINT)
WCLASS(punct, PUNCT)
WCLASS(space, SPACE)
WCLASS(upper, UPPER)
WCLASS(xdigit, XDIGIT)

extern "C" OSV_LIBC_API
wctype_t wctype(const char *s) noexcept
{
    return make_wctype(s, OSV_CUR_UTF8);
}

extern "C" OSV_LIBC_API
wctype_t __wctype_l(const char *s, locale_t l) noexcept
{
    return make_wctype(s, __osv_ctype_utf8(l));
}
weak_alias(__wctype_l, wctype_l);

extern "C" OSV_LIBC_API
int iswctype(wint_t wc, wctype_t t) noexcept
{
    unsigned long cls = t & ~utf8_bit;
    return cls >= ALNUM && cls <= XDIGIT ? classify(wc, int(cls), t & utf8_bit) : 0;
}

extern "C" OSV_LIBC_API
int __iswctype_l(wint_t wc, wctype_t t, locale_t) noexcept
{
    return iswctype(wc, t);
}
weak_alias(__iswctype_l, iswctype_l);

extern "C" OSV_LIBC_API
wint_t towupper(wint_t wc) noexcept
{
    return map(wc, TOUPPER, OSV_CUR_UTF8);
}

extern "C" OSV_LIBC_API
wint_t __towupper_l(wint_t wc, locale_t l) noexcept
{
    return map(wc, TOUPPER, __osv_ctype_utf8(l));
}
weak_alias(__towupper_l, towupper_l);

extern "C" OSV_LIBC_API
wint_t towlower(wint_t wc) noexcept
{
    return map(wc, TOLOWER, OSV_CUR_UTF8);
}

extern "C" OSV_LIBC_API
wint_t __towlower_l(wint_t wc, locale_t l) noexcept
{
    return map(wc, TOLOWER, __osv_ctype_utf8(l));
}
weak_alias(__towlower_l, towlower_l);

extern "C" OSV_LIBC_API
wctrans_t wctrans(const char *s) noexcept
{
    return make_wctrans(s, OSV_CUR_UTF8);
}

extern "C" OSV_LIBC_API
wctrans_t __wctrans_l(const char *s, locale_t l) noexcept
{
    return make_wctrans(s, __osv_ctype_utf8(l));
}
weak_alias(__wctrans_l, wctrans_l);

extern "C" OSV_LIBC_API
wint_t towctrans(wint_t wc, wctrans_t t) noexcept
{
    auto v = reinterpret_cast<unsigned long>(t);
    unsigned long how = v & ~utf8_bit;
    return how == TOUPPER || how == TOLOWER ? map(wc, int(how), v & utf8_bit) : wc;
}

extern "C" OSV_LIBC_API
wint_t __towctrans_l(wint_t wc, wctrans_t t, locale_t) noexcept
{
    return towctrans(wc, t);
}
weak_alias(__towctrans_l, towctrans_l);

extern "C" OSV_LIBC_API
int wcwidth(wchar_t wc) noexcept
{
    if (!OSV_CUR_UTF8 && uint32_t(wc) >= 0x80) {
        return -1;
    }
    return __osv_utf8_wcwidth(wc);
}

extern "C" OSV_LIBC_API
int wcswidth(const wchar_t *s, size_t n) noexcept
{
    int total = 0;
    for (; n-- && *s; s++) {
        int w = wcwidth(*s);
        if (w < 0) {
            return -1;
        }
        total += w;
    }
    return total;
}
