/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// Host differential of a loaded LC_CTYPE: glibc's answers against OSv's
// table lookups (libc/locale/locale-data.cc) over the same file, for every
// code point 0..0x10ffff and 0x110000, 0x7fffffff, 0xffffffff: each class
// the locale names (wctype/iswctype), each map (wctrans/towctrans), the 12
// standard classes, towupper, towlower, wcwidth, and the byte tables
// (__ctype_b, toupper, tolower at -128..255).
//   g++ -std=gnu++17 -O2 -Ilibc/locale scripts/locale-oracle/ctype-diff.cc
//       libc/locale/locale-data.cc -o ctype-diff
//   LOCPATH=<dir> ./ctype-diff <locale>...     exit 1 if anything differs
#include "locale-data.h"

#include <cstdio>
#include <cstring>
#include <ctype.h>
#include <langinfo.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>

int main(int argc, char **argv)
{
    static const char *const std_names[12] = { "alnum", "alpha", "blank", "cntrl", "digit", "graph",
                                               "lower", "print", "punct", "space", "upper", "xdigit" };
    long bad = 0;
    for (int a = 1; a < argc; a++) {
        if (!setlocale(LC_ALL, argv[a])) {
            printf("NOLOCALE %s\n", argv[a]);
            bad++;
            continue;
        }
        const osv_locale_data *d = osv_locale_load(LC_CTYPE, argv[a]);
        if (!d) {
            printf("OSV-NOLOAD %s\n", argv[a]);
            bad++;
            continue;
        }
        const osv_ctype *c = &d->ctype;
        long diff = 0, checks = 0;
        auto check = [&](bool same, const char *what, unsigned long cp, long g, long o) {
            checks++;
            if (!same && ++diff < 20) {
                printf("DIFF %s %s %lx glibc %lx osv %lx\n", argv[a], what, cp, g, o);
            }
        };
        static const unsigned long extra[] = { 0x110000, 0x7fffffff, 0xffffffff };
        auto each = [&](auto f) {
            for (unsigned long cp = 0; cp <= 0x10ffff; cp++) f(cp);
            for (unsigned long cp : extra) f(cp);
        };
        const char *cn = nl_langinfo(_NL_CTYPE_CLASS_NAMES), *mn = nl_langinfo(_NL_CTYPE_MAP_NAMES);
        for (const char *p = cn; *p; p += strlen(p) + 1) {
            wctype_t t = wctype(p);
            const char *o = osv_ctype_find(c, p, false);
            check(t && o, p, 0, !!t, !!o);
            each([&](unsigned long cp) { check(!!iswctype(cp, t) == osv_ctype_class(o, cp), p, cp, !!iswctype(cp, t), osv_ctype_class(o, cp)); });
        }
        for (const char *p = mn; *p; p += strlen(p) + 1) {
            wctrans_t t = wctrans(p);
            const char *o = osv_ctype_find(c, p, true);
            check(t && o, p, 0, !!t, !!o);
            each([&](unsigned long cp) { check(towctrans(cp, t) == osv_ctype_map(o, cp), p, cp, towctrans(cp, t), osv_ctype_map(o, cp)); });
        }
        for (int k = 0; k < 12; k++) {
            wctype_t t = wctype(std_names[k]);
            each([&](unsigned long cp) { check(!!iswctype(cp, t) == osv_ctype_class(c->cls[k], cp), std_names[k], cp, 0, 0); });
        }
        each([&](unsigned long cp) {
            check(towupper(cp) == osv_ctype_map(c->toupper, cp), "towupper", cp, towupper(cp), osv_ctype_map(c->toupper, cp));
            check(towlower(cp) == osv_ctype_map(c->tolower, cp), "towlower", cp, towlower(cp), osv_ctype_map(c->tolower, cp));
            check(wcwidth(cp) == osv_ctype_width(c->width, cp), "wcwidth", cp, wcwidth(cp), osv_ctype_width(c->width, cp));
        });
        const unsigned short *gb = *__ctype_b_loc();
        const int *gu = *__ctype_toupper_loc(), *gl = *__ctype_tolower_loc();
        for (int i = -128; i < 256; i++) {
            check(gb[i] == c->class_b[i], "ctype_b", i, gb[i], c->class_b[i]);
            check(gu[i] == c->toupper_b[i], "toupper_b", i, gu[i], c->toupper_b[i]);
            check(gl[i] == c->tolower_b[i], "tolower_b", i, gl[i], c->tolower_b[i]);
        }
        printf("%s %ld checks %ld differ\n", argv[a], checks, diff);
        bad += diff;
    }
    return bad != 0;
}
