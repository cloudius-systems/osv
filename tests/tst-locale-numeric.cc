/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// LC_NUMERIC in printf, scanf and strtod (Stage 1b): the decimal point, the
// ' flag's grouping and glibc's I flag, in de_DE, fr_FR (multibyte
// separator U+202F), fa_IR (Persian outdigits), en_US and C; the results
// are glibc 2.41's (scripts/locale-oracle/numeric-diff). Runs on OSv and,
// built with g++ against glibc, on Linux:
//   g++ -std=gnu++17 -DOSV_CONF_LOCALE=1 tests/tst-locale-numeric.cc
//   OSV_LOCALE_TEST_DATA=<make-test-locales.sh output> ./a.out
// With conf_locale=0 the named locales do not exist: the test reports SKIP.

#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cwchar>

#include <locale.h>
#include <sys/stat.h>

#ifndef OSV_CONF_LOCALE
#define OSV_CONF_LOCALE 1
#endif

// OSv exports strtod_l (libc/locale/strtod_l.c); its <stdlib.h> does not
// declare it
extern "C" double strtod_l(const char *, char **, locale_t);

static int tests, fails, skips;

static void report(bool ok, const std::string &what)
{
    tests++;
    if (!ok) {
        fails++;
    }
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

static std::string esc(const char *s)
{
    std::string r;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char b[8];
        if (*p < 32 || *p > 126) {
            snprintf(b, sizeof b, "\\x%02x", *p);
            r += b;
        } else {
            r += char(*p);
        }
    }
    return r;
}

// snprintf(fmt, args) == want (and the return value is strlen(want))
template <typename... A>
static void pf(const char *loc, const char *want, const char *fmt, A... a)
{
    char b[256];
    int r = snprintf(b, sizeof b, fmt, a...);
    report(r == int(strlen(want)) && !strcmp(b, want),
           std::string(loc) + " printf \"" + fmt + "\" = \"" + esc(b) + "\" want \"" + esc(want) + "\"");
}

// swprintf(fmt, args) == want
template <typename... A>
static void wpf(const char *loc, const wchar_t *want, const wchar_t *fmt, A... a)
{
    wchar_t b[256];
    int r = swprintf(b, 256, fmt, a...);
    char fs[64], ws[256], gs[256];
    snprintf(fs, sizeof fs, "%ls", fmt);
    std::string g, w;
    for (const wchar_t *p = b; *p; p++) {
        snprintf(gs, sizeof gs, *p < 127 ? "%c" : "\\u%04x", unsigned(*p));
        g += gs;
    }
    for (const wchar_t *p = want; *p; p++) {
        snprintf(ws, sizeof ws, *p < 127 ? "%c" : "\\u%04x", unsigned(*p));
        w += ws;
    }
    report(r == int(wcslen(want)) && !wcscmp(b, want),
           std::string(loc) + " swprintf \"" + fs + "\" = \"" + g + "\" want \"" + w + "\"");
}

// strtod(in) == v, consuming n bytes
static void st(const char *loc, const char *in, double v, int n)
{
    char *e;
    double d = strtod(in, &e);
    char b[160];
    snprintf(b, sizeof b, "%s strtod(\"%s\") = %a +%d want %a +%d", loc, esc(in).c_str(), d, int(e - in), v, n);
    report(d == v && e - in == n, b);
    float f = strtof(in, &e);
    snprintf(b, sizeof b, "%s strtof(\"%s\") = %a +%d", loc, esc(in).c_str(), double(f), int(e - in));
    report(f == float(v) && e - in == n, b);
    long double ld = strtold(in, &e);
    snprintf(b, sizeof b, "%s strtold(\"%s\") +%d", loc, esc(in).c_str(), int(e - in));
    report(ld == (long double)v || (std::fabs(double(ld - v)) < 1e-12 * std::fabs(v) && e - in == n), b);
}

// sscanf(in, "%<fl>lf%n") == (r, v, n)
static void sf(const char *loc, const char *fmt, const char *in, int r, double v, int n)
{
    double d = -7;
    int k = -7;
    int got = sscanf(in, fmt, &d, &k);
    char b[200];
    snprintf(b, sizeof b, "%s sscanf(\"%s\", \"%s\") = %d %.17g +%d want %d %.17g +%d", loc, esc(in).c_str(), fmt,
             got, d, k, r, v, n);
    report(got == r && (r < 1 || d == v) && k == n, b);
}

static void si(const char *loc, const char *fmt, const char *in, int r, long v, int n)
{
    long d = -7;
    int k = -7;
    int got = sscanf(in, fmt, &d, &k);
    char b[200];
    snprintf(b, sizeof b, "%s sscanf(\"%s\", \"%s\") = %d %ld +%d want %d %ld +%d", loc, esc(in).c_str(), fmt, got, d,
             k, r, v, n);
    report(got == r && (r < 1 || d == v) && k == n, b);
}

static void swf(const char *loc, const wchar_t *fmt, const wchar_t *in, int r, double v, int n)
{
    double d = -7;
    int k = -7;
    int got = swscanf(in, fmt, &d, &k);
    char b[200];
    snprintf(b, sizeof b, "%s swscanf(\"%ls\", \"%ls\") = %d %.17g +%d want %d %.17g +%d", loc, in, fmt, got, d, k,
             r, v, n);
    report(got == r && (r < 1 || d == v) && k == n, b);
}

// localeconv() agrees with what printf and strtod use
static void agree(const char *loc)
{
    struct lconv *lc = localeconv();
    char b[64], want[64];
    snprintf(b, sizeof b, "%.1f", 1.5);
    snprintf(want, sizeof want, "1%s5", lc->decimal_point);
    report(!strcmp(b, want), std::string(loc) + " localeconv decimal_point \"" + esc(lc->decimal_point) +
           "\" agrees with printf \"" + esc(b) + "\"");
    char *e;
    double d = strtod(want, &e);
    report(d == 1.5 && *e == 0, std::string(loc) + " strtod reads printf's \"" + esc(want) + "\"");
    snprintf(b, sizeof b, "%'d", 1234567);
    if (*lc->thousands_sep && *lc->grouping == 3) {
        snprintf(want, sizeof want, "1%s234%s567", lc->thousands_sep, lc->thousands_sep);
    } else {
        snprintf(want, sizeof want, "1234567");
    }
    report(!strcmp(b, want), std::string(loc) + " localeconv thousands_sep \"" + esc(lc->thousands_sep) +
           "\" agrees with %'d \"" + esc(b) + "\"");
}

static bool use(const char *loc)
{
    if (!setlocale(LC_ALL, loc)) {
        report(false, std::string("setlocale(LC_ALL, \"") + loc + "\")");
        return false;
    }
    return true;
}

static void test_c()
{
    for (const char *loc : { "C", "C.UTF-8" }) {
        if (!use(loc)) {
            continue;
        }
        pf(loc, "1234567.891000", "%f", 1234567.891);
        pf(loc, "1234567", "%'d", 1234567);
        pf(loc, "1234567.9", "%'.1f", 1234567.891);
        pf(loc, "0x1.8p+0", "%a", 1.5);
        pf(loc, "1.5e+00", "%.1e", 1.5);
        st(loc, "1.5", 1.5, 3);
        st(loc, "1,5", 1, 1);
        sf(loc, "%lf%n", "1.5", 1, 1.5, 3);
        // C has no separator: ' changes nothing. With conf_locale=0 OSv's
        // scanf is musl's, which rejects ' (-1), unchanged from before 1b.
        if (OSV_CONF_LOCALE) {
            sf(loc, "%'lf%n", "1,234.5", 1, 1, 1);
        } else {
            sf(loc, "%'lf%n", "1,234.5", -1, 0, -7);
        }
        agree(loc);
    }
}

static void test_de()
{
    const char *loc = "de_DE.UTF-8";
    if (!use(loc)) {
        return;
    }
    pf(loc, "1234567,891000", "%f", 1234567.891);
    pf(loc, "1.234.567,891000", "%'f", 1234567.891);
    pf(loc, "1,234568e+06", "%e", 1234567.891);
    pf(loc, "1,23457e+06", "%'g", 1234567.891);
    pf(loc, "12.345,5", "%'g", 12345.5);
    pf(loc, "0x1,8p+0", "%a", 1.5);
    pf(loc, "1.234.567", "%'d", 1234567);
    pf(loc, "-2.147.483.648", "%'d", -2147483647 - 1);
    pf(loc, "0001.234.567", "%'.10d", 1234567);
    pf(loc, "000001.234.567", "%'014d", 1234567);
    pf(loc, "00001.234.567,891000", "%'020f", 1234567.891);
    pf(loc, "12d.687", "%'x", 0x12d687);
    pf(loc, "0x12d687", "%'p", (void *)0x12d687);
    pf(loc, "1234568,", "%#.0f", 1234567.891);
    pf(loc, "inf -nan", "%'f %'f", INFINITY, -NAN);
    pf(loc, "9.223.372.036.854.775.807", "%'lld", 9223372036854775807LL);
    wpf(loc, L"1.234.567,891000", L"%'f", 1234567.891);
    wpf(loc, L"     1.234.567", L"%'14d", 1234567);
    st(loc, "1,5", 1.5, 3);
    st(loc, "1.5", 1, 1);
    st(loc, " -,5", -0.5, 4);
    st(loc, "0x1,8p1", 3, 7);
    st(loc, "1.234,5", 1, 1);
    sf(loc, "%lf%n", "1,5", 1, 1.5, 3);
    sf(loc, "%lf%n", "1.5", 1, 1, 1);
    sf(loc, "%'lf%n", "1.234,5", 1, 1234.5, 7);
    sf(loc, "%'lf%n", "1.234.567,5", 1, 1234567.5, 11);
    sf(loc, "%'lf%n", "1.2345", 1, 1234, 6);
    sf(loc, "%'lf%n", "1234.567", 1, 1234, 8);
    sf(loc, "%'lf%n", "1.23e3", 1, 1, 6);
    sf(loc, "%'7lf%n", "1.234.567", 1, 1234, 7);
    si(loc, "%'ld%n", "1.234.567", 1, 1234567, 9);
    si(loc, "%'ld%n", "123.45.678", 1, 123, 10);
    si(loc, "%ld%n", "1.234", 1, 1, 1);
    swf(loc, L"%'lf%n", L"1.234,5", 1, 1234.5, 7);
    agree(loc);
    // the _l variants take their own locale, not the current one
    locale_t de = newlocale(LC_ALL_MASK, loc, 0);
    setlocale(LC_ALL, "C");
    char *e;
    const char *in = "1,5";
    double d = strtod_l(in, &e, de);
    report(de && d == 1.5 && e - in == 3, "strtod_l(\"1,5\", de_DE) in C = 1.5 +3");
    d = strtod(in, &e);
    report(d == 1 && e - in == 1, "strtod(\"1,5\") in C = 1 +1");
    uselocale(de);
    pf("uselocale(de_DE)", "1,5", "%.1f", 1.5);
    uselocale(LC_GLOBAL_LOCALE);
    pf("after uselocale(LC_GLOBAL_LOCALE)", "1.5", "%.1f", 1.5);
    if (de) {
        freelocale(de);
    }
}

static void test_fr()
{
    const char *loc = "fr_FR.UTF-8";
    if (!use(loc)) {
        return;
    }
    pf(loc, "1\xe2\x80\xaf" "234\xe2\x80\xaf" "567,891000", "%'f", 1234567.891);
    pf(loc, " 1\xe2\x80\xaf" "234\xe2\x80\xaf" "567", "%'14d", 1234567);  // width counts bytes
    pf(loc, "    1\xe2\x80\xaf" "234\xe2\x80\xaf" "567,891000", "%'20f", 1234567.891);  // characters
    wpf(loc, L"1\u202f234\u202f567,891000", L"%'f", 1234567.891);
    wpf(loc, L"     1\u202f234\u202f567", L"%'14d", 1234567);
    sf(loc, "%'lf%n", "1\xe2\x80\xaf" "234,5", 1, 1234.5, 9);
    si(loc, "%'ld%n", "1\xe2\x80\xaf" "5", 1, 15, 5);  // narrow: groups not checked
    swf(loc, L"%'lf%n", L"1\u202f234,5", 1, 1234.5, 7);
    st(loc, "1\xe2\x80\xaf" "234", 1, 1);
    agree(loc);
}

static void test_fa()
{
    const char *loc = "fa_IR.UTF-8";
    if (!use(loc)) {
        return;
    }
    pf(loc, "1,234,567.891000", "%'f", 1234567.891);
    // I: the LC_CTYPE outdigits (U+06F0..U+06F9)
    pf(loc, "\xdb\xb1\xdb\xb2\xdb\xb3\xdb\xb4", "%Id", 1234);
    pf(loc, "\xdb\xb1,\xdb\xb2\xdb\xb3\xdb\xb4", "%'Id", 1234);
    pf(loc, "-\xdb\xb0.\xdb\xb5", "%I.1f", -0.5);
    pf(loc, "0000\xdb\xb1", "%I.5d", 1);  // precision zeros stay ASCII
    pf(loc, "\xdb\xb1.\xdb\xb5\xdb\xb0" "e+\xdb\xb0\xdb\xb0", "%I.2e", 1.5);
    pf(loc, "0x1.8p+0", "%Ia", 1.5);  // not a, x, o
    pf(loc, "12d687", "%Ix", 0x12d687);
    wpf(loc, L"\u06f1,\u06f2\u06f3\u06f4", L"%'Id", 1234);
    agree(loc);
}

static void test_en()
{
    const char *loc = "en_US.UTF-8";
    if (!use(loc)) {
        return;
    }
    pf(loc, "1,234,567.891000", "%'f", 1234567.891);
    pf(loc, "1.23457e+06", "%'g", 1234567.891);
    pf(loc, "1234567", "%Id", 1234567);
    st(loc, "1.5", 1.5, 3);
    st(loc, "1,5", 1, 1);
    sf(loc, "%'lf%n", "12,345,678.5", 1, 12345678.5, 12);
    si(loc, "%'ld%n", "1,234", 1, 1234, 5);
    agree(loc);
}

static bool exists(const std::string &p)
{
    struct stat st;
    return !stat(p.c_str(), &st);
}

int main()
{
    unsetenv("LC_ALL");
    unsetenv("LANG");
    const char *d = getenv("OSV_LOCALE_TEST_DATA");
    std::string data = d ? d : "/usr/share/osv-locale-test";
    test_c();
    if (!OSV_CONF_LOCALE) {
        skips++;
        printf("SKIP: built with conf_locale=0: no named locales, LC_NUMERIC is C's\n");
    } else if (!exists(data + "/dir/de_DE.utf8/LC_NUMERIC")) {
        // conf_locale=1 images must ship the test locales (make-test-locales.sh
        // hard-fails the build without localedef); absent data here is a real
        // defect, not a SKIP. A gate that cannot fail is not a gate.
        fails++;
        printf("FAIL: conf_locale=1 but no test locale data in %s "
               "(build should have failed without localedef)\n", data.c_str());
    } else {
        setenv("LOCPATH", (data + "/dir").c_str(), 1);
        test_de();
        test_fr();
        test_fa();
        test_en();
        setlocale(LC_ALL, "C");
    }
    printf("SUMMARY: %d tests, %d failures, %d skipped\n", tests, fails, skips);
    return fails ? 1 : 0;
}
