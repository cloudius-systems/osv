/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// glibc's C.UTF-8 wide-character classes, case mappings and widths, and its
// C-locale wide-output transliteration, as lookups over tables generated
// from measurements of glibc 2.41 (scripts/locale-oracle/README.md):
//   ctype-utf8.inc  by scripts/locale-oracle/gen-ctype-tables.py
//   translit-c.inc  by scripts/locale-oracle/gen-translit.py
// scripts/locale-oracle/check-tables.cc proves they reproduce the measured
// output for every code point.
//
// The classes and widths share one sorted run table: ctype_lo[i] is the
// first code point of run i, which reaches to ctype_lo[i+1] (the last run to
// 0x10ffff), and ctype_val[i] packs the run's 12-bit class mask with its
// width (bits 12-13 hold width + 1). One binary search answers all 12
// iswctype classes and wcwidth. Case maps stay as their own run tables.

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include <algorithm>

namespace {
struct casemap {
    uint32_t key; // lo << 11 | alt << 10 | (n - 1)
    int32_t delta;
};
#include "ctype-utf8.inc"
#include "translit-c.inc"

// The ctype_val entry whose run contains wc (wc <= 0x10ffff; the table
// covers 0..0x10ffff with no gaps, so this is always valid).
uint16_t ctype_lookup(uint32_t wc)
{
    constexpr size_t N = sizeof(ctype_lo) / sizeof(*ctype_lo);
    auto p = std::upper_bound(ctype_lo, ctype_lo + N, wc);
    return ctype_val[(p - ctype_lo) - 1];
}

uint32_t casemap_lookup(const casemap* t, size_t n, wint_t wc)
{
    if (wc > 0x10ffff) {
        return wc;
    }
    auto p = std::upper_bound(t, t + n, uint32_t(wc << 11 | 0x7ff),
        [](uint32_t k, const casemap& e) { return k < e.key; });
    if (p == t) {
        return wc;
    }
    --p;
    uint32_t off = wc - (p->key >> 11), last = p->key & 0x3ff;
    if (p->key & 0x400) {
        if (off & 1) {
            return wc;
        }
        off >>= 1;
    }
    return off <= last ? wc + p->delta : wc;
}
}

#define HIDDEN extern "C" __attribute__((visibility("hidden")))

// cls is a wctype() value: 1 alnum .. 12 xdigit, bit cls-1 of the class mask.
HIDDEN int __osv_utf8_iswctype(wint_t wc, int cls)
{
    if (cls < 1 || cls > 12 || wc > 0x10ffff) {
        return 0;
    }
    return (ctype_lookup(uint32_t(wc)) >> (cls - 1)) & 1;
}

HIDDEN wint_t __osv_utf8_towupper(wint_t wc)
{
    return casemap_lookup(case_upper, sizeof(case_upper) / sizeof(*case_upper), wc);
}

HIDDEN wint_t __osv_utf8_towlower(wint_t wc)
{
    return casemap_lookup(case_lower, sizeof(case_lower) / sizeof(*case_lower), wc);
}

HIDDEN int __osv_utf8_wcwidth(wchar_t wc)
{
    if (uint32_t(wc) > 0x10ffff) {
        return -1;
    }
    return int(ctype_lookup(uint32_t(wc)) >> 12) - 1;
}

// What glibc's C locale writes on a wide stream for wc >= 0x80: false when
// it has no entry (the caller writes '?'), else *len bytes at *out (possibly
// none).
HIDDEN bool __osv_c_translit(wchar_t wc, const char** out, size_t* len)
{
    auto e = tr_cp + sizeof(tr_cp) / sizeof(*tr_cp);
    auto p = std::lower_bound(tr_cp, e, uint32_t(wc));
    if (p == e || *p != uint32_t(wc)) {
        return false;
    }
    *out = tr_str + tr_off[p - tr_cp];
    *len = tr_off[p - tr_cp + 1] - tr_off[p - tr_cp];
    return true;
}
