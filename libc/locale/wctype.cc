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
//
// With conf_locale=1, a loaded LC_CTYPE (a named locale) answers from its
// own tables (libc/locale/locale-data.cc and .h),
// as glibc does: every class and map it names (ja_JP's jhira, tojkata ...),
// tr_TR's i/I mappings, its widths. Its descriptors are pointers to its
// tables, which are never unmapped; the built-in ones are small integers.

#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <osv/export.h>

#include "libc/internal/locale_impl.h"
#include "libc/internal/libc.h"

#if OSV_CONF_LOCALE
#include "locale-data.h"
#endif

extern "C" OSV_HIDDEN int __osv_utf8_iswctype(wint_t wc, int cls);
extern "C" OSV_HIDDEN wint_t __osv_utf8_towupper(wint_t wc);
extern "C" OSV_HIDDEN wint_t __osv_utf8_towlower(wint_t wc);
extern "C" OSV_HIDDEN int __osv_utf8_wcwidth(wchar_t wc);

namespace {

#if OSV_CONF_LOCALE
// The tables of l's LC_CTYPE if it is a loaded locale, else null.
const osv_ctype *loaded(locale_t l)
{
    auto d = static_cast<const osv_locale_data *>(__osv_ctype_data(l));
    return d ? &d->ctype : nullptr;
}

// A descriptor that points at a table (an address in mapped data) rather
// than a built-in class or map number.
bool is_table(unsigned long v)
{
    return v > 0xffff;
}
#endif

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

namespace {

int classify_l(wint_t wc, int cls, locale_t l)
{
#if OSV_CONF_LOCALE
    if (const osv_ctype *c = loaded(l)) {
        return osv_ctype_class(c->cls[cls - 1], wc);
    }
#endif
    return classify(wc, cls, __osv_ctype_utf8(l));
}

wint_t map_l(wint_t wc, int how, locale_t l)
{
#if OSV_CONF_LOCALE
    if (const osv_ctype *c = loaded(l)) {
        return osv_ctype_map(how == TOUPPER ? c->toupper : c->tolower, wc);
    }
#endif
    return map(wc, how, __osv_ctype_utf8(l));
}

}

#define WCLASS(name, cls)                                                    \
extern "C" OSV_LIBC_API int isw##name(wint_t wc) noexcept                    \
{                                                                            \
    return classify_l(wc, cls, nullptr);                                     \
}                                                                            \
extern "C" OSV_LIBC_API int __isw##name##_l(wint_t wc, locale_t l) noexcept  \
{                                                                            \
    return classify_l(wc, cls, l);                                           \
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
wctype_t __wctype_l(const char *s, locale_t l) noexcept
{
#if OSV_CONF_LOCALE
    if (const osv_ctype *c = loaded(l)) {
        return reinterpret_cast<wctype_t>(osv_ctype_find(c, s, false));
    }
#endif
    return make_wctype(s, __osv_ctype_utf8(l));
}
weak_alias(__wctype_l, wctype_l);

extern "C" OSV_LIBC_API
wctype_t wctype(const char *s) noexcept
{
    return __wctype_l(s, nullptr);
}

extern "C" OSV_LIBC_API
int iswctype(wint_t wc, wctype_t t) noexcept
{
#if OSV_CONF_LOCALE
    if (is_table(t)) {
        return osv_ctype_class(reinterpret_cast<const char *>(t), wc);
    }
#endif
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
    return map_l(wc, TOUPPER, nullptr);
}

extern "C" OSV_LIBC_API
wint_t __towupper_l(wint_t wc, locale_t l) noexcept
{
    return map_l(wc, TOUPPER, l);
}
weak_alias(__towupper_l, towupper_l);

extern "C" OSV_LIBC_API
wint_t towlower(wint_t wc) noexcept
{
    return map_l(wc, TOLOWER, nullptr);
}

extern "C" OSV_LIBC_API
wint_t __towlower_l(wint_t wc, locale_t l) noexcept
{
    return map_l(wc, TOLOWER, l);
}
weak_alias(__towlower_l, towlower_l);

extern "C" OSV_LIBC_API
wctrans_t __wctrans_l(const char *s, locale_t l) noexcept
{
#if OSV_CONF_LOCALE
    if (const osv_ctype *c = loaded(l)) {
        return reinterpret_cast<wctrans_t>(osv_ctype_find(c, s, true));
    }
#endif
    return make_wctrans(s, __osv_ctype_utf8(l));
}
weak_alias(__wctrans_l, wctrans_l);

extern "C" OSV_LIBC_API
wctrans_t wctrans(const char *s) noexcept
{
    return __wctrans_l(s, nullptr);
}

extern "C" OSV_LIBC_API
wint_t towctrans(wint_t wc, wctrans_t t) noexcept
{
    auto v = reinterpret_cast<unsigned long>(t);
#if OSV_CONF_LOCALE
    if (is_table(v)) {
        return osv_ctype_map(reinterpret_cast<const char *>(t), wc);
    }
#endif
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
#if OSV_CONF_LOCALE
    if (const osv_ctype *c = loaded(nullptr)) {
        return osv_ctype_width(c->width, uint32_t(wc));
    }
#endif
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
