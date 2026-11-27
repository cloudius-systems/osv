/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Host differential of OSv's collation (libc/locale/collate.cc, on the
// LC_COLLATE that libc/locale/locale-data.cc loads) against glibc's
// strcoll/strxfrm/wcscoll/wcsxfrm on the same compiled data. Build and run
// from the OSv tree:
//
//   g++ -std=gnu++17 -O2 -DOSV_COLLATE_HOST -Ilibc/locale scripts/locale-oracle/collate/coll-diff.cc \
//       libc/locale/collate.cc libc/locale/locale-data.cc -o coll-diff
//   LOCPATH=<dir> ./coll-diff [-r <random pairs>] [-s <seed>] [-q] <locale>...
//
// For each locale, on (a) a fixed word list (every pair), (b) random pairs of
// strings over a mixed alphabet with invalid UTF-8, (c) every pair of single
// code points from a sampled set, (d) wide strings with values mbstowcs never
// gives (surrogates, past 0x10ffff, negative), (e) long position counts at
// the UTF-8 length boundaries, (f) strings of about 4096 elements, it checks:
//   - sign(osv_strcoll) == sign(glibc strcoll), and the same for wcscoll;
//   - osv_strxfrm bytes == glibc strxfrm bytes, the same for wcsxfrm, and the
//     returned lengths with n = 0 (the needed length; nothing written);
//   - the pairs where sign(strcmp(strxfrm)) differs from sign(strcoll) are
//     the same pairs for OSv and glibc (counted; their sets are equal if the
//     two checks above hold, the count is reported).
// It also checks that strxfrm never writes past n (n = 0 .. length + 1 on a
// sample). Exit 1 if anything differs.
//
// The probes in this directory (xf.c, signs.c and model.py) measured the rules
// collate.cc implements (STAGE1-FORMAT-SPEC.md section 6).

#include "locale-data.h"

#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale.h>
#include <string>
#include <vector>
#include <wchar.h>

namespace {

long differ, checks, glibc_incons, osv_incons;
bool quiet;

int sign(long x)
{
    return (x > 0) - (x < 0);
}

std::string hex(const std::string &s)
{
    std::string r;
    char b[3];
    for (unsigned char ch : s) {
        snprintf(b, sizeof b, "%02x", ch);
        r += b;
    }
    return r;
}

void report(const char *what, const std::string &a, const std::string &b, long g, long o)
{
    differ++;
    if (!quiet && differ <= 40) {
        printf("DIFF %s [%s] [%s] glibc=%ld osv=%ld\n", what, hex(a).c_str(), hex(b).c_str(), g, o);
    }
}

// mbstowcs of s, or false (as glibc's, so both sides see the same wide string).
bool widen(const std::string &s, std::wstring *w)
{
    size_t n = mbstowcs(nullptr, s.c_str(), 0);
    if (n == size_t(-1)) {
        return false;
    }
    w->resize(n);
    mbstowcs(&(*w)[0], s.c_str(), n + 1);
    return true;
}

struct item {
    std::string s, gx, ox;          // string, glibc and OSv strxfrm
    bool wide;
    std::wstring w, gwx, owx;
};

std::string glibc_xfrm(const std::string &s)
{
    size_t n = strxfrm(nullptr, s.c_str(), 0);
    std::string r(n + 1, '\0');
    strxfrm(&r[0], s.c_str(), n + 1);
    r.resize(n);
    return r;
}

std::string osv_xfrm(const osv_collate *c, const std::string &s)
{
    size_t n = osv_strxfrm(c, nullptr, s.c_str(), 0);
    std::string r(n + 2, '\x7f');
    size_t m = osv_strxfrm(c, &r[0], s.c_str(), n + 1);
    if (m != n || r[n] != '\0' || r[n + 1] != '\x7f') {
        report("strxfrm-length-or-terminator", s, "", long(n), long(m));
    }
    r.resize(n);
    return r;
}

std::wstring glibc_wxfrm(const std::wstring &w)
{
    size_t n = wcsxfrm(nullptr, w.c_str(), 0);
    std::wstring r(n + 1, L'\0');
    wcsxfrm(&r[0], w.c_str(), n + 1);
    r.resize(n);
    return r;
}

std::wstring osv_wxfrm(const osv_collate *c, const std::wstring &w)
{
    size_t n = osv_wcsxfrm(c, nullptr, w.c_str(), 0);
    std::wstring r(n + 2, wchar_t(0x7f));
    size_t m = osv_wcsxfrm(c, &r[0], w.c_str(), n + 1);
    if (m != n || r[n] != 0 || r[n + 1] != 0x7f) {
        report("wcsxfrm-length-or-terminator", "", "", long(n), long(m));
    }
    r.resize(n);
    return r;
}

item make(const osv_collate *c, const std::string &s)
{
    item it;
    it.s = s;
    it.gx = glibc_xfrm(s);
    it.ox = osv_xfrm(c, s);
    checks++;
    if (it.gx != it.ox) {
        report("strxfrm", s, "", long(it.gx.size()), long(it.ox.size()));
    }
    it.wide = widen(s, &it.w);
    if (it.wide) {
        it.gwx = glibc_wxfrm(it.w);
        it.owx = osv_wxfrm(c, it.w);
        checks++;
        if (it.gwx != it.owx) {
            report("wcsxfrm", s, "", long(it.gwx.size()), long(it.owx.size()));
        }
    }
    return it;
}

void pair(const osv_collate *c, const item &a, const item &b)
{
    int g = sign(strcoll(a.s.c_str(), b.s.c_str()));
    int o = sign(osv_strcoll(c, a.s.c_str(), b.s.c_str()));
    checks++;
    if (g != o) {
        report("strcoll", a.s, b.s, g, o);
    }
    int gx = sign(a.gx.compare(b.gx)), ox = sign(a.ox.compare(b.ox));
    glibc_incons += gx != g;
    osv_incons += ox != o;
    if ((gx != g) != (ox != o)) {
        report("strxfrm-vs-strcoll-set", a.s, b.s, gx != g, ox != o);
    }
    if (a.wide && b.wide) {
        int gw = sign(wcscoll(a.w.c_str(), b.w.c_str()));
        int ow = sign(osv_wcscoll(c, a.w.c_str(), b.w.c_str()));
        checks++;
        if (gw != ow) {
            report("wcscoll", a.s, b.s, gw, ow);
        }
        int gwx = sign(a.gwx.compare(b.gwx)), owx = sign(a.owx.compare(b.owx));
        if ((gwx != gw) != (owx != ow)) {
            report("wcsxfrm-vs-wcscoll-set", a.s, b.s, gwx != gw, owx != ow);
        }
    }
}

// No write past n, for n = 0 .. length + 1.
void bounds(const osv_collate *c, const std::string &s)
{
    size_t need = osv_strxfrm(c, nullptr, s.c_str(), 0);
    std::string full = glibc_xfrm(s);
    for (size_t n = 0; n <= need + 1; n++) {
        std::string buf(need + 8, '\x7f');
        size_t r = osv_strxfrm(c, &buf[0], s.c_str(), n);
        checks++;
        bool ok = r == need;
        for (size_t k = n; k < buf.size(); k++) {
            ok = ok && buf[k] == '\x7f';
        }
        // The units written are the output's first ones (C leaves them
        // indeterminate when n <= need; OSv's choice).
        ok = ok && !buf.compare(0, std::min(n, need), full, 0, std::min(n, need));
        if (!ok) {
            report("strxfrm-bounds", s, "", long(n), long(r));
        }
    }
    std::wstring w;
    if (!widen(s, &w)) {
        return;
    }
    need = osv_wcsxfrm(c, nullptr, w.c_str(), 0);
    for (size_t n = 0; n <= need + 1; n++) {
        std::wstring buf(need + 8, wchar_t(0x7f));
        size_t r = osv_wcsxfrm(c, &buf[0], w.c_str(), n);
        checks++;
        bool ok = r == need;
        for (size_t k = n; k < buf.size(); k++) {
            ok = ok && buf[k] == 0x7f;
        }
        if (!ok) {
            report("wcsxfrm-bounds", s, "", long(n), long(r));
        }
    }
}

std::string utf8(uint32_t cp)
{
    std::string r;
    if (cp < 0x80) {
        r += char(cp);
    } else if (cp < 0x800) {
        r += char(0xc0 | cp >> 6);
        r += char(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        r += char(0xe0 | cp >> 12);
        r += char(0x80 | ((cp >> 6) & 0x3f));
        r += char(0x80 | (cp & 0x3f));
    } else {
        r += char(0xf0 | cp >> 18);
        r += char(0x80 | ((cp >> 12) & 0x3f));
        r += char(0x80 | ((cp >> 6) & 0x3f));
        r += char(0x80 | (cp & 0x3f));
    }
    return r;
}

// (a) Accents, case, punctuation, digits, spaces, hyphens, ligatures, CJK,
// Cyrillic, Greek, Arabic, Thai, Hangul, contractions (cs_CZ ch, da_DK aa,
// hr_HR dz/lj/nj, sk_SK ch), and invalid UTF-8.
const char *const words[] = {
    "", " ", "  ", "-", "--", "'", "a", "A", "b", "B", "z", "Z", "aa", "Aa", "AA", "ab", "aB", "Ab",
    "a b", "a-b", "a'b", "ab-", "-ab", "a  b", "a\tb", "co-op", "coop", "co op", "Co-op", "cote",
    "cot\xc3\xa9", "c\xc3\xb4te", "c\xc3\xb4t\xc3\xa9", "cote-d'or", "e", "E", "\xc3\xa9", "\xc3\x89",
    "\xc3\xa8", "\xc3\xaa", "\xc3\xab", "e\xcc\x81", "E\xcc\x81", "\xc3\xa9t\xc3\xa9", "ete",
    "\xc3\xa9te", "et\xc3\xa9", "resume", "r\xc3\xa9sum\xc3\xa9", "Resume", "R\xc3\xa9sum\xc3\xa9",
    "na\xc3\xafve", "naive", "\xc3\xa6", "ae", "AE", "\xc3\x86", "\xc5\x93", "oe", "\xc3\x9f", "ss",
    "SS", "\xef\xac\x81", "fi", "\xef\xac\x82", "fl", "\xc3\xa5", "aa", "\xc3\xa4", "\xc3\xb6", "o",
    "\xc3\xb8", "\xc3\xbc", "u", "\xc3\xb1", "n", "ny", "\xc5\x82", "l", "\xc4\xb1", "i", "I",
    "\xc4\xb0", "c", "C", "ch", "Ch", "CH", "cH", "h", "H", "cz", "hz", "chz", "\xc4\x8d", "\xc4\x8c",
    "\xc5\x99", "r", "\xc5\xa1", "s", "\xc5\xbe", "dz", "d\xc5\xbe", "lj", "nj", "\xc4\x91", "d",
    "0", "1", "2", "9", "10", "01", "1.5", "1,5", "-1", "+1", "$1", "\xe2\x82\xac" "1", "1a", "a1",
    "1st", "\xc2\xbd", "\xc2\xb2", "\xd9\xa1", "\xe0\xb9\x91", "\xe4\xb8\x80", "\xe4\xba\x8c",
    "\xe4\xb8\x89", "\xe5\x8d\x81", "\xe4\xb8\xad\xe6\x96\x87", "\xe6\x97\xa5\xe6\x9c\xac",
    "\xe3\x81\x82", "\xe3\x82\xa2", "\xe3\x81\x8b", "\xe3\x82\xab", "\xef\xbd\xb1", "\xea\xb0\x80",
    "\xea\xb0\x81", "\xe1\x84\x80", "\xd0\xb0", "\xd0\x90", "\xd0\xb1", "\xd1\x91", "\xd0\xb5",
    "\xd0\x81", "\xd0\xb9", "\xd0\xb8", "\xce\xb1", "\xce\x91", "\xce\xac", "\xd8\xa7", "\xd8\xa8",
    "\xd9\x93", "\xd8\xa2", "\xe0\xb8\x81", "\xe0\xb9\x80\xe0\xb8\x81", "\xe0\xb8\x81\xe0\xb8\xb2",
    "\xe0\xb8\xaf", "\xf0\x9f\x98\x80", "\xef\xbf\xbd", "\xc2\xa0", "\xe2\x80\x90", "\xc2\xb7",
    "l\xc2\xb7l", "ll", "\x01", "\x7f", "\x80", "\xff", "\xc2", "\xe4\xb8", "\xed\xa0\x80",
    "\xf4\x90\x80\x80", "\xc0\xaf", "a\x80", "\x80" "a", "\xcc\x81", "\xcc\x81\xcc\x80",
    "\xcc\x80 A", "\xcc\x81-e", "x\xcc\x81\xcc\x80y", "\xc3\xa9\xc3\xa8", "\xc3\xa8\xc3\xa9",
    "PostgreSQL", "postgres", "Postgres", "zzz", "Zebra", "zebra", "\xc3\x85ngstr\xc3\xb6m",
    "Angstrom", "M\xc3\xbcller", "Mueller", "Muller", "O'Brien", "OBrien", "o'brien",
};

const char *const alphabet[] = {
    "a", "b", "c", "h", "C", "H", "A", "e", "E", "\xc3\xa9", "\xc3\xa8", "\xc3\xaa", "o", "\xc3\xb4",
    "-", " ", "'", "1", "2", "$", "\xcc\x81", "\xcc\x80", "\xcc\x82", "\xe4\xb8\x80", "\xe4\xb8\x81",
    "L", "l", "\xc2\xb7", "\xd0\xb0", "\xd0\x90", "\xd8\xa7", "\xd9\x93", "\xe0\xb8\xaf",
    "\xe0\xb9\x80", "\xe0\xb8\x81", "\x01", "\t", "\xc3\x9f", "\xc3\xa6", "\xc4\xb1", "I", "i",
    "\xc4\xb0", "\xe1\x84\x80", "\xea\xb0\x80", "\xe3\x81\x82", "\xe3\x82\xa2", "\xf0\x9f\x98\x80",
    "\xef\xbf\xbd", "\xc2\xa0", "\xe2\x80\x90", "\xc2\xbd", "d", "z", "\xc4\x8d", "\xc5\xbe", "j",
    "n", "\xc3\xa5", "\x80", "\xff", "\xc2", "\xe4\xb8", "\xed\xa0\x80", "\xf4\x90\x80\x80",
};

uint64_t rng_state;
uint32_t rng()
{
    rng_state = rng_state * 6364136223846793005ull + 1442695040888963407ull;
    return uint32_t(rng_state >> 33);
}

std::string random_string()
{
    std::string s;
    unsigned len = rng() % 9;
    for (unsigned k = 0; k < len; k++) {
        s += alphabet[rng() % (sizeof alphabet / sizeof *alphabet)];
    }
    return s;
}

// (c) Code points sampled across the planes: every 0x80th, plus the
// boundaries and every one of a few dense blocks (Latin, combining marks).
std::vector<uint32_t> sample_code_points()
{
    std::vector<uint32_t> v;
    for (uint32_t cp = 1; cp < 0x250; cp++) {
        v.push_back(cp);
    }
    for (uint32_t cp = 0x300; cp < 0x370; cp++) {
        v.push_back(cp);
    }
    for (uint32_t cp = 0x370; cp < 0x110000; cp += 0x7f) {
        if (cp < 0xd800 || cp > 0xdfff) {
            v.push_back(cp);
        }
    }
    for (uint32_t cp : { 0xd7ffu, 0xe000u, 0xfffdu, 0xfffeu, 0xffffu, 0x10000u, 0x10fffdu,
                         0x10ffffu, 0x1f600u, 0xe0100u }) {
        v.push_back(cp);
    }
    return v;
}

int run(const char *name, long npairs)
{
    long d0 = differ, c0 = checks, gi0 = glibc_incons, oi0 = osv_incons;
    if (!setlocale(LC_ALL, name)) {
        printf("%s: glibc cannot load it\n", name);
        return 1;
    }
    const osv_locale_data *d = osv_locale_load(LC_COLLATE, name);
    if (!d) {
        printf("%s: OSv cannot load LC_COLLATE\n", name);
        return 1;
    }
    const osv_collate *c = &d->collate;
    if (!c->nrules) {
        printf("%s: no levels (byte order), skipped\n", name);
        return 0;
    }
    // (a) every pair of the word list, both orders.
    std::vector<item> w;
    for (const char *s : words) {
        w.push_back(make(c, s));
        bounds(c, s);
    }
    for (auto &a : w) {
        for (auto &b : w) {
            pair(c, a, b);
        }
    }
    long a_checks = checks - c0;
    // (b) random pairs.
    for (long k = 0; k < npairs; k++) {
        item a = make(c, random_string()), b = make(c, random_string());
        pair(c, a, b);
        if (k % 1000 == 0) {
            bounds(c, a.s);
        }
    }
    long b_checks = checks - c0 - a_checks;
    // (c) every pair of single code points from the sample, one order.
    std::vector<item> cps;
    for (uint32_t cp : sample_code_points()) {
        cps.push_back(make(c, utf8(cp)));
    }
    for (size_t i = 0; i < cps.size(); i++) {
        for (size_t j = i + 1; j < cps.size(); j++) {
            pair(c, cps[i], cps[j]);
        }
    }
    long c_checks = checks - c0 - a_checks - b_checks;
    // (e) long position counts: k characters empty at the first levels
    // (U+4E00 in the 4-level locales: its later levels are empty) before
    // "a", at each UTF-8 length boundary, including the 5-byte form.
    // glibc's strxfrm is far slower than linear on one long backward run of
    // ignorable characters (in the custom locale bp a 2^21-element run ran for
    // 29 minutes inside glibc's strxfrm_l; the shipped locales finish), so
    // the custom locales (make-custom-locales.sh: names without '_') stop at
    // 65536.
    bool custom = !strchr(name, '_');
    if (c->nrules > 1) {
        for (size_t k : { 126, 127, 128, 2046, 2047, 2048, 65534, 65535, 65536, 2097150, 2097151 }) {
            if (custom && k > 65536) {
                continue;
            }
            std::string x;
            for (size_t i = 0; i < k; i++) {
                x += "\xe4\xb8\x80";
            }
            x += "a";
            checks++;
            if (glibc_xfrm(x) != osv_xfrm(c, x)) {
                report("strxfrm-long-count", "", "", long(k), 0);
            }
            std::wstring wx(k, wchar_t(0x4e00));
            wx += L"a";
            checks++;
            if (glibc_wxfrm(wx) != osv_wxfrm(c, wx)) {
                report("wcsxfrm-long-count", "", "", long(k), 0);
            }
            std::string y = x.substr(3) + "\xe4\xb8\x80";
            checks++;
            if (sign(strcoll(x.c_str(), y.c_str())) != sign(osv_strcoll(c, x.c_str(), y.c_str()))) {
                report("strcoll-long-count", "", "", long(k), 0);
            }
        }
    }
    // (f) strings around 4096 elements (glibc's strxfrm changes a rule there,
    // STAGE1-FORMAT-SPEC.md 6.4): N - 1 copies of a piece, then a last piece,
    // for N = 4094..4098, over the pieces below (ignorable, accented,
    // combining, contraction, CJK); strxfrm, wcsxfrm and strcoll against the
    // same string with its last piece changed.
    for (const char *piece : { "-", " ", "a", "\xc3\xa9", "\xcc\x81", "ch", "\xe4\xb8\x80", "\x01" }) {
        for (const char *last : { "a", "-", "\xcc\x81", "\xc3\xa9" }) {
            for (size_t N = 4094; N <= 4098; N++) {
                std::string x;
                for (size_t i = 0; i + 1 < N; i++) {
                    x += piece;
                }
                std::string y = x + "b";
                x += last;
                item ix = make(c, x), iy = make(c, y);
                pair(c, ix, iy);
                pair(c, iy, ix);
            }
        }
    }
    // (d) wide strings mbstowcs never produces: surrogates, values past
    // 0x10ffff, negative wchar_t; with neighbours, every pair.
    std::vector<std::wstring> odd;
    for (uint32_t u : { 0xd800u, 0xdfffu, 0xfffeu, 0x110000u, 0x1fffffu, 0x7fffffffu, 0x80000000u,
                        0xffffffffu, 0xe0100u, 0x10fffdu }) {
        odd.push_back(std::wstring(1, wchar_t(u)));
        odd.push_back(std::wstring(1, wchar_t(u)) + L"a");
        odd.push_back(L"a" + std::wstring(1, wchar_t(u)) + L"\u0301");
    }
    for (const std::wstring &x : odd) {
        checks++;
        if (glibc_wxfrm(x) != osv_wxfrm(c, x)) {
            report("wcsxfrm-odd", "", "", long(x[0]), 0);
        }
        for (const std::wstring &y : odd) {
            checks++;
            if (sign(wcscoll(x.c_str(), y.c_str())) != sign(osv_wcscoll(c, x.c_str(), y.c_str()))) {
                report("wcscoll-odd", "", "", long(x[0]), long(y[0]));
            }
        }
    }
    printf("%s: %ld differ in %ld checks (words %ld, random %ld pairs %ld, code points %zu: %ld);"
           " strxfrm-vs-strcoll disagreements glibc %ld osv %ld\n",
           name, differ - d0, checks - c0, a_checks, npairs, b_checks, cps.size(), c_checks,
           glibc_incons - gi0, osv_incons - oi0);
    return differ != d0;
}

} // namespace

int main(int argc, char **argv)
{
    long npairs = 100000;
    rng_state = 1;
    int a = 1;
    for (; a < argc && argv[a][0] == '-'; a++) {
        if (!strcmp(argv[a], "-r") && a + 1 < argc) {
            npairs = atol(argv[++a]);
        } else if (!strcmp(argv[a], "-s") && a + 1 < argc) {
            rng_state = strtoull(argv[++a], nullptr, 0);
        } else if (!strcmp(argv[a], "-q")) {
            quiet = true;
        }
    }
    int rc = 0;
    for (; a < argc; a++) {
        rc |= run(argv[a], npairs);
    }
    printf("SUMMARY %ld differ in %ld checks\n", differ, checks);
    return rc || differ;
}
