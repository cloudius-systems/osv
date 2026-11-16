/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Collation in named locales (Stage 1c, conf_locale=1): for each locale of
// tst-locale-collate-words.h, the word list sorted with strcoll must be the
// list glibc sorted (tst-locale-collate.inc, generated on Linux from the
// same compiled data by scripts/locale-oracle/collate/gen-collate-expected.c),
// adjacent strcoll and wcscoll signs must be glibc's, and every word's strxfrm
// and wcsxfrm output must have glibc's length and hash. Also: the _l forms
// and uselocale() agree, strxfrm(NULL, s, 0) gives the length, nothing is
// written at or past n, and a named LC_COLLATE alone (LC_ALL stays C) is
// used. C and C.UTF-8 are in the list too: byte and code point order.
//
// The locales come from LOCPATH=/usr/share/osv-locale-test/dir in the test
// image (make-test-locales.sh). With conf_locale=0 (or no test data) named
// locales are not available and only C and C.UTF-8 are checked.
//
// Runs on Linux too: LOCPATH=<out of make-test-locales.sh>/dir ./tst-locale-collate

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <locale.h>
#include <sys/stat.h>

#include "tst-locale-collate-words.h"
#include "tst-locale-collate.inc"

#ifndef OSV_CONF_LOCALE
#define OSV_CONF_LOCALE 1
#endif

namespace {

int tests, fails, skips;

void report(bool ok, const std::string &what)
{
    tests++;
    if (!ok) {
        fails++;
        printf("FAIL: %s\n", what.c_str());
    }
}

int sign(int x)
{
    return (x > 0) - (x < 0);
}

constexpr size_t nwords = sizeof collate_words / sizeof *collate_words;

std::string show(const char *s)
{
    std::string r;
    char b[8];
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; p++) {
        if (*p >= 0x20 && *p < 0x7f) {
            r += char(*p);
        } else {
            snprintf(b, sizeof b, "\\x%02x", *p);
            r += b;
        }
    }
    return "\"" + r + "\"";
}

uint64_t whash(const wchar_t *w, size_t n)
{
    uint64_t h = COLLATE_FNV0;
    for (size_t k = 0; k < n; k++) {
        uint32_t u = uint32_t(w[k]);
        unsigned char b[4] = { (unsigned char)(u & 0xff), (unsigned char)((u >> 8) & 0xff),
                               (unsigned char)((u >> 16) & 0xff), (unsigned char)(u >> 24) };
        h = collate_fnv(b, 4, h);
    }
    return h;
}

// A stable merge sort with strcoll_l, as the generator sorts with strcoll.
void merge_sort(int *v, int *tmp, int n, locale_t l)
{
    if (n < 2) {
        return;
    }
    int h = n / 2;
    merge_sort(v, tmp, h, l);
    merge_sort(v + h, tmp, n - h, l);
    int i = 0, j = h, k = 0;
    while (i < h && j < n) {
        tmp[k++] = strcoll_l(collate_words[v[j]], collate_words[v[i]], l) < 0 ? v[j++] : v[i++];
    }
    while (i < h) {
        tmp[k++] = v[i++];
    }
    while (j < n) {
        tmp[k++] = v[j++];
    }
    memcpy(v, tmp, n * sizeof *v);
}

void test_locale(int li)
{
    const char *name = collate_locales[li];
    const collate_expect *e = expect_all[li];
    locale_t l = newlocale(LC_ALL_MASK, name, nullptr);
    if (!l) {
        // li 0,1 are the C/C.UTF-8 builtins (always available). A named locale
        // (li>=2) that will not load at conf_locale=1 means the test image is
        // missing its data; make-test-locales.sh hard-fails the build without
        // localedef, so this is a real defect, not a SKIP. A gate that cannot
        // fail is not a gate. At conf_locale=0 named locales are unavailable by
        // design, so SKIP.
        if (OSV_CONF_LOCALE && li >= 2) {
            fails++;
            printf("FAIL: conf_locale=1 but %s not available "
                   "(missing test locale data?)\n", name);
        } else {
            skips++;
            printf("SKIP: %s not available\n", name);
        }
        return;
    }
    std::string ln = name;
    // Sorted order, by strcoll_l.
    int v[nwords], tmp[nwords];
    for (size_t i = 0; i < nwords; i++) {
        v[i] = int(i);
    }
    merge_sort(v, tmp, int(nwords), l);
    int bad = 0;
    for (size_t i = 0; i < nwords; i++) {
        if (v[i] != e[i].word && bad++ < 3) {
            printf("  %s position %zu: %s, glibc %s\n", name, i, show(collate_words[v[i]]).c_str(),
                   show(collate_words[e[i].word]).c_str());
        }
    }
    report(!bad, ln + " sorted list equals glibc's (" + std::to_string(bad) + " positions differ)");
    // Adjacent signs, strxfrm and wcsxfrm, in glibc's order.
    int sbad = 0, wsbad = 0, xbad = 0, wxbad = 0;
    for (size_t i = 0; i < nwords; i++) {
        const char *s = collate_words[e[i].word];
        if (i + 1 < nwords) {
            const char *t = collate_words[e[i + 1].word];
            sbad += sign(strcoll_l(s, t, l)) != e[i].coll;
            if (e[i].wcoll != 2) {
                wchar_t ws[256], wt[256];
                locale_t old = uselocale(l);
                mbstowcs(ws, s, 256);
                mbstowcs(wt, t, 256);
                uselocale(old);
                wsbad += sign(wcscoll_l(ws, wt, l)) != e[i].wcoll;
            }
        }
        unsigned char x[4096];
        size_t n = strxfrm_l(reinterpret_cast<char *>(x), s, sizeof x, l);
        if (n != e[i].n || collate_fnv(x, n, COLLATE_FNV0) != e[i].xh) {
            if (xbad++ < 3) {
                printf("  %s strxfrm %s: length %zu, glibc %zu\n", name, show(s).c_str(), n, e[i].n);
            }
        }
        if (e[i].wn >= 0) {
            wchar_t ws[256], wx[1024];
            locale_t old = uselocale(l);
            mbstowcs(ws, s, 256);
            uselocale(old);
            size_t wn = wcsxfrm_l(wx, ws, 1024, l);
            if (long(wn) != e[i].wn || whash(wx, wn) != e[i].wh) {
                if (wxbad++ < 3) {
                    printf("  %s wcsxfrm %s: length %zu, glibc %ld\n", name, show(s).c_str(), wn,
                           e[i].wn);
                }
            }
        }
    }
    report(!sbad, ln + " strcoll adjacent signs (" + std::to_string(sbad) + " differ)");
    report(!wsbad, ln + " wcscoll adjacent signs (" + std::to_string(wsbad) + " differ)");
    report(!xbad, ln + " strxfrm bytes (" + std::to_string(xbad) + " differ)");
    report(!wxbad, ln + " wcsxfrm units (" + std::to_string(wxbad) + " differ)");
    // The current-locale forms agree with the _l forms (uselocale), and
    // LC_COLLATE alone decides (setlocale of LC_COLLATE only).
    int ubad = 0, cbad = 0;
    locale_t old = uselocale(l);
    for (size_t i = 0; i + 1 < nwords; i++) {
        const char *s = collate_words[e[i].word], *t = collate_words[e[i + 1].word];
        ubad += sign(strcoll(s, t)) != e[i].coll;
    }
    uselocale(old);
    std::string saved = setlocale(LC_ALL, nullptr);
    setlocale(LC_ALL, "C");
    if (setlocale(LC_COLLATE, name)) {
        for (size_t i = 0; i + 1 < nwords; i++) {
            const char *s = collate_words[e[i].word], *t = collate_words[e[i + 1].word];
            cbad += sign(strcoll(s, t)) != e[i].coll;
            unsigned char x[4096];
            size_t n = strxfrm(reinterpret_cast<char *>(x), s, sizeof x);
            cbad += n != e[i].n || collate_fnv(x, n, COLLATE_FNV0) != e[i].xh;
        }
    } else {
        cbad = -1;
    }
    setlocale(LC_ALL, saved.c_str());
    report(!ubad, ln + " strcoll under uselocale (" + std::to_string(ubad) + " differ)");
    report(!cbad, ln + " LC_COLLATE alone (" + std::to_string(cbad) + " differ)");
    // Length rule: strxfrm(NULL, s, 0) gives the length and writes nothing;
    // with n <= length nothing is written at or past n; with n > length the
    // output ends in a NUL at the length.
    int nbad = 0;
    for (size_t i = 0; i < nwords; i++) {
        const char *s = collate_words[e[i].word];
        size_t need = strxfrm_l(nullptr, s, 0, l);
        nbad += need != e[i].n;
        for (size_t n = 0; n <= need + 1; n++) {
            char b[4200];
            memset(b, 0x7f, sizeof b);
            size_t r = strxfrm_l(b, s, n, l);
            nbad += r != need;
            for (size_t k = n; k < need + 8; k++) {
                nbad += b[k] != 0x7f;
            }
            if (n > need) {
                nbad += b[need] != 0;
            }
        }
        if (e[i].wn >= 0) {
            wchar_t ws[256];
            old = uselocale(l);
            mbstowcs(ws, s, 256);
            uselocale(old);
            size_t wneed = wcsxfrm_l(nullptr, ws, 0, l);
            nbad += long(wneed) != e[i].wn;
            for (size_t n = 0; n <= wneed + 1; n++) {
                wchar_t b[1100];
                wmemset(b, 0x7f, 1100);
                size_t r = wcsxfrm_l(b, ws, n, l);
                nbad += r != wneed;
                for (size_t k = n; k < wneed + 8; k++) {
                    nbad += b[k] != 0x7f;
                }
                if (n > wneed) {
                    nbad += b[wneed] != 0;
                }
            }
        }
    }
    report(!nbad, ln + " strxfrm/wcsxfrm length and bounds (" + std::to_string(nbad) + " wrong)");
    freelocale(l);
}

// Strings longer than the stack buffer (4096 elements): the heap path, and
// a long string of ignorable characters (large position counts).
void test_long()
{
    locale_t l = newlocale(LC_ALL_MASK, "en_US.UTF-8", nullptr);
    if (!l) {
        skips++;
        printf("SKIP: en_US.UTF-8 not available (long strings)\n");
        return;
    }
    std::string a(10000, 'a'), b = a;
    b[9999] = 'b';
    report(strcoll_l(a.c_str(), b.c_str(), l) < 0 && strcoll_l(b.c_str(), a.c_str(), l) > 0 &&
           strcoll_l(a.c_str(), a.c_str(), l) == 0, "en_US long strings strcoll");
    // en_US: '-' is ignorable at the first three levels, so 300 of them then
    // "a" equals "a" there; at the fourth (position) level '-' weighs less
    // than 'a', so it sorts first (glibc: -1).
    std::string h = std::string(300, '-') + "a";
    report(strcoll_l(h.c_str(), "a", l) < 0 && strcoll_l("a", h.c_str(), l) > 0,
           "en_US 300 hyphens then a, before a");
    // 5000 U+0001 (empty at every level but the last) between two letters:
    // a position count above 0x7f, 2 bytes of UTF-8 (glibc: before "ab").
    std::string p = "a" + std::string(5000, '\x01') + "b";
    report(strcoll_l(p.c_str(), "ab", l) < 0 && strcoll_l("ab", p.c_str(), l) > 0,
           "en_US a, 5000 U+0001, b before ab");
    size_t n = strxfrm_l(nullptr, h.c_str(), 0, l);
    std::vector<char> x(n + 1);
    report(strxfrm_l(x.data(), h.c_str(), n + 1, l) == n && x[n] == 0,
           "en_US long strxfrm length " + std::to_string(n));
    // Ignorable-only strings sort after "" (measured on glibc).
    report(strcoll_l("", "-", l) < 0 && strcoll_l("-", "", l) > 0 && strcoll_l("", "", l) == 0,
           "en_US empty string first");
    freelocale(l);
}

bool exists(const char *p)
{
    struct stat st;
    return !stat(p, &st);
}

} // namespace

int main()
{
    if (!getenv("LOCPATH") && exists("/usr/share/osv-locale-test/dir")) {
        setenv("LOCPATH", "/usr/share/osv-locale-test/dir", 1);
    }
    for (size_t li = 0; li < sizeof collate_locales / sizeof *collate_locales; li++) {
        test_locale(int(li));
    }
    test_long();
    printf("SUMMARY: %d tests, %d failures, %d skipped\n", tests, fails, skips);
    return fails ? 1 : 0;
}
