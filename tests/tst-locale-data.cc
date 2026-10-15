/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Named locales from glibc-format locale data (OSv conf_locale=1).
//
// The data is what scripts/locale-oracle/make-test-locales.sh compiles with
// the build host's localedef, installed in the test image under
// /usr/share/osv-locale-test/ (the 14 default locales as directories and as
// an archive, a Latin-1 locale, UTF-8 data under a Latin-1 name) and
// /usr/lib/locale (da_DK.utf8, and the archive copy as locale-archive).
// Every expectation was taken from glibc 2.41 with the same data, and the
// test passes there too: build it on Linux with g++ -std=c++17
// -fno-builtin-toupper -fno-builtin-tolower and run it as
//   OSV_LOCALE_TEST_DATA=<out of make-test-locales.sh> ./tst-locale-data
// (the archive and default-directory checks then use whatever the host has
// in /usr/lib/locale; they report SKIP when it has no locale-archive with
// de_DE.utf8 or no da_DK.utf8).
//
// Built for OSv, modules/tests/Makefile passes -DOSV_CONF_LOCALE=0 or 1 as
// the kernel was configured. With 0 there are no named locales (and no data
// in the image): the test checks that every named locale is ENOENT. With 1
// the data must be in the image: missing data, archive or default directory
// is a FAIL on OSv, and a SKIP only on Linux (the host's /usr/lib/locale).

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <locale>
#include <string>
#include <thread>

#include <langinfo.h>
#include <locale.h>
#include <monetary.h>
#include <sys/stat.h>
#include <wchar.h>
#include <wctype.h>

#ifndef OSV_CONF_LOCALE
#define OSV_CONF_LOCALE 1   // glibc
#endif

#ifndef _NL_ITEM
#define _NL_ITEM(cat, idx) (((cat) << 16) | (idx))
#endif
#ifdef __OSV__
// glibc's <ctype.h> declares these; OSv's does not, though it exports them.
extern "C" const int32_t **__ctype_toupper_loc(void);
extern "C" const int32_t **__ctype_tolower_loc(void);
#endif
#ifndef LC_IDENTIFICATION
#define LC_IDENTIFICATION 12    // glibc's numbering (OSv's <locale.h> lacks the name)
#endif

static int tests, fails, skips;

static void report(bool ok, const std::string &what)
{
    tests++;
    if (!ok) {
        fails++;
    }
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

// Missing data: a FAIL on an OSv conf_locale=1 image, which must have it,
// else a SKIP.
static void skip(const std::string &what)
{
#if defined(__OSV__) && OSV_CONF_LOCALE
    report(false, "missing on OSv conf_locale=1: " + what);
#else
    skips++;
    printf("SKIP: %s\n", what.c_str());
#endif
}

static std::string show(const char *s)
{
    return s ? std::string("\"") + s + "\"" : "NULL";
}

static bool exists(const std::string &p)
{
    struct stat st;
    return !stat(p.c_str(), &st);
}

static const char *const env_vars[] = { "LC_ALL", "LANG", "LANGUAGE", "LC_CTYPE", "LC_NUMERIC",
    "LC_TIME", "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES", "LC_PAPER", "LC_NAME", "LC_ADDRESS",
    "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION" };

static std::string data_dir;

static void set_locpath(const char *sub)
{
    if (sub) {
        setenv("LOCPATH", (data_dir + "/" + sub).c_str(), 1);
    } else {
        unsetenv("LOCPATH");
    }
}

// setlocale(cat, name): the result and errno against the expectation.
static void expect_set(int cat, const char *name, const char *want, int want_errno,
                       const std::string &tag)
{
    setlocale(LC_ALL, "C");
    errno = 0;
    const char *r = setlocale(cat, name);
    int e = errno;
    bool ok = want ? r && !strcmp(r, want) : !r && e == want_errno;
    report(ok, tag + ": setlocale(" + std::to_string(cat) + ", " + show(name) + ") = " + show(r) +
           (r ? "" : " errno " + std::to_string(e)));
}

static void test_conf0()
{
    // conf_locale=0: no named locales at all, whatever data is installed.
    set_locpath("dir");
    for (const char *n : { "de_DE.UTF-8", "de_DE.utf8", "en_US.UTF-8", "ja_JP.UTF-8" }) {
        expect_set(LC_ALL, n, nullptr, ENOENT, "conf_locale=0");
        errno = 0;
        report(!newlocale(LC_ALL_MASK, n, nullptr) && errno == ENOENT,
               std::string("conf_locale=0: newlocale ") + n + " ENOENT");
    }
    set_locpath(nullptr);
    expect_set(LC_ALL, "de_DE.UTF-8", nullptr, ENOENT, "conf_locale=0 archive");
    expect_set(LC_ALL, "C.UTF-8", "C.UTF-8", 0, "conf_locale=0 built-in");
}

static void test_locpath()
{
    set_locpath("dir");
    // Name as requested, codeset normalised to find the directory.
    for (const char *n : { "de_DE.UTF-8", "de_DE.utf8", "de_DE.UTF8", "de_DE.utf-8", "de_DE.Utf8",
                           "de_DE.UTF+8" }) {
        expect_set(LC_ALL, n, n, 0, "LOCPATH");
    }
    // Not found, or not UTF-8. (de_DE@euro: the fallback drops the
    // modifier, but de_DE without a codeset is not there either.)
    for (const char *n : { "de_DE", "de_DE.U-T-F-8", "de_DE.utf_8", "de_DE@euro", "de.UTF-8",
                           "xx_YY.UTF-8", "de_DE.latin1", "de_DE.UTF-8.x", "de_DE..utf8" }) {
        expect_set(LC_ALL, n, nullptr, ENOENT, "LOCPATH");
    }
    // A locale that is not UTF-8 (de_DE in ISO-8859-1): glibc loads it; OSv
    // supports only UTF-8 locales so far, and it is ENOENT there. The one
    // intended difference from glibc.
    for (const char *n : { "xx_L1", "xx_L1.ISO-8859-1", "xx_L1.UTF-8" }) {
#ifdef __OSV__
        expect_set(LC_ALL, n, nullptr, ENOENT, "not UTF-8 (OSv)");
#else
        expect_set(LC_ALL, n, strcmp(n, "xx_L1.UTF-8") ? n : nullptr, ENOENT, "not UTF-8 (glibc)");
#endif
    }
    // Codeset mismatch: de_DE.ISO-8859-1 exists but holds UTF-8 data.
    report(exists(data_dir + "/dir/de_DE.ISO-8859-1/LC_CTYPE"), "de_DE.ISO-8859-1 data exists");
    expect_set(LC_ALL, "de_DE.ISO-8859-1", nullptr, ENOENT, "codeset mismatch");
    // EINVAL names.
    for (const char *n : { "de_DE.utf8/", "..", "de_DE.UTF-8/../x" }) {
        expect_set(LC_ALL, n, nullptr, EINVAL, "LOCPATH");
    }
    // One category; the others stay C.
    expect_set(LC_TIME, "fr_FR.UTF-8", "fr_FR.UTF-8", 0, "LOCPATH LC_TIME");
    setlocale(LC_ALL, "C");
    setlocale(LC_TIME, "fr_FR.UTF-8");
    report(!strcmp(nl_langinfo(DAY_2), "lundi") && !strcmp(nl_langinfo(D_FMT), "%d/%m/%Y") &&
           !strcmp(nl_langinfo(CODESET), "ANSI_X3.4-1968") && MB_CUR_MAX == 1,
           "LC_TIME fr_FR only: DAY_2 " + show(nl_langinfo(DAY_2)) + " CODESET " +
           show(nl_langinfo(CODESET)));
    // A failed category leaves everything as it was.
    errno = 0;
    report(!setlocale(LC_NUMERIC, "xx_YY.UTF-8") && errno == ENOENT &&
           !strcmp(setlocale(LC_TIME, nullptr), "fr_FR.UTF-8") &&
           !strcmp(setlocale(LC_NUMERIC, nullptr), "C"), "failed LC_NUMERIC changes nothing");
    errno = 0;
    report(!setlocale(LC_ALL, "xx_YY.UTF-8") && errno == ENOENT &&
           !strcmp(setlocale(LC_TIME, nullptr), "fr_FR.UTF-8"), "failed LC_ALL changes nothing");
    setlocale(LC_ALL, "C");
    // Values from the data.
    setlocale(LC_ALL, "de_DE.UTF-8");
    char buf[128];
    struct tm tm = {};
    tm.tm_year = 126;
    tm.tm_mday = 4;
    strftime(buf, sizeof(buf), "%A %d. %B %Y", &tm);
    struct lconv *lc = localeconv();
    report(!strcmp(nl_langinfo(CODESET), "UTF-8") && MB_CUR_MAX == 6 &&
           !strcmp(nl_langinfo(D_FMT), "%d.%m.%Y") && !strcmp(nl_langinfo(DAY_1), "Sonntag") &&
           !strcmp(nl_langinfo(YESEXPR), "^[+1jJyY]") && !strcmp(buf, "Sonntag 04. Januar 2026") &&
           !strcmp(lc->decimal_point, ",") && !strcmp(lc->thousands_sep, ".") &&
           !strcmp(lc->currency_symbol, "\u20ac") && lc->int_frac_digits == 2 &&
           lc->p_cs_precedes == 0 && !strcmp(lc->grouping, "\3"),
           "de_DE.UTF-8 values: strftime " + show(buf) + " decimal_point " + show(lc->decimal_point));
    report(!strcmp(nl_langinfo(_NL_ITEM(LC_TIME, 0xffff)), "de_DE.UTF-8"),
           "_NL_LOCALE_NAME(LC_TIME) " + show(nl_langinfo(_NL_ITEM(LC_TIME, 0xffff))));
    // Byte ctype stays C in a UTF-8 locale, as glibc's.
    report(!isalpha(0xe9) && toupper(0xe9) == 0xe9 && toupper('a') == 'A',
           "byte ctype in de_DE.UTF-8 is C's");
    // strptime round trip of %c in ja_JP.
    setlocale(LC_ALL, "ja_JP.UTF-8");
    tm = {};
    tm.tm_year = 126;
    tm.tm_mon = 6;
    tm.tm_mday = 15;
    tm.tm_hour = 13;
    tm.tm_min = 45;
    tm.tm_sec = 30;
    tm.tm_wday = 3;
    strftime(buf, sizeof(buf), "%c", &tm);
    struct tm p = {};
    const char *end = strptime(buf, "%c", &p);
    report(end && !*end && p.tm_year == 126 && p.tm_mon == 6 && p.tm_mday == 15 &&
           p.tm_hour == 13 && p.tm_min == 45 && p.tm_sec == 30,
           "ja_JP %c round trip " + show(buf));
    setlocale(LC_ALL, "C");
    // Several LOCPATH directories: the first that has the name wins; a
    // missing directory is skipped.
    setenv("LOCPATH", ("/nonexistent:" + data_dir + "/dir").c_str(), 1);
    expect_set(LC_ALL, "it_IT.UTF-8", "it_IT.UTF-8", 0, "LOCPATH with a missing dir");
}

// The directory lookup drops the modifier, then the territory,
// each with the codeset as given, normalised and dropped (measured on glibc
// 2.41: de_AT.UTF-8@euro tries de_AT.UTF-8@euro de_AT.utf8@euro de_AT@euro
// de.UTF-8@euro de.utf8@euro de@euro de_AT.UTF-8 de_AT.utf8 de_AT de.UTF-8
// de.utf8 de). fb/ has de.utf8 (sv_SE data), de@euro (it_IT), de_AT
// (fr_FR) and de_CH.utf8@euro (es_ES); the archive is not searched this way.
// The LC_IDENTIFICATION title ("Italian locale for Italy") shows the source.
static void test_fallback()
{
    setenv("LOCPATH", (data_dir + "/fb:" + data_dir + "/dir").c_str(), 1);
    static const char *const cases[][2] = {
        { "de_AT.UTF-8@euro", "Italian" }, { "de_AT.UTF-8", "French" }, { "de_AT", "French" },
        { "de_AT@euro", "Italian" }, { "de_CH.UTF-8@euro", "Spanish" }, { "de_CH.UTF8@euro", "Spanish" },
        { "de_CH.UTF-8", "Swedish" }, { "de_CH@euro", "Italian" }, { "de.UTF-8", "Swedish" },
        { "de@euro", "Italian" }, { "de_DE.UTF-8@euro", "Italian" }, { "de_.UTF-8", "Swedish" },
        { "de_AT_x.UTF-8", "Swedish" }, { "de_AT@euro@x", "French" }, { "de_AT.UTF-8@", "French" },
        { "de_DE.UTF-8", "German" }, { "fa_IR.UTF-8@x", "Persian" },
        { "de", nullptr }, { "de_LU@x", nullptr }, { "de_AT.ISO-8859-1", nullptr },
        { "_AT.UTF-8", nullptr }, { "de_AT.@euro", nullptr }, { "de.UTF-8.x", nullptr },
        { "de@", nullptr },
    };
    for (auto &c : cases) {
        setlocale(LC_ALL, "C");
        errno = 0;
        const char *r = setlocale(LC_ALL, c[0]);
        int e = errno;
        const char *title = r ? nl_langinfo(_NL_ITEM(LC_IDENTIFICATION, 0)) : "";
        // LC_IDENTIFICATION's first item, the title, names the source locale.
        bool ok = c[1] ? r && !strcmp(r, c[0]) && strstr(title, c[1]) : !r && e == ENOENT;
        report(ok, std::string("fallback ") + c[0] + " -> " + show(r) + " " + show(title) +
               " want " + show(c[1]));
    }
    // The archive does not fall back.
    set_locpath(nullptr);
    if (exists("/usr/lib/locale/locale-archive")) {
        expect_set(LC_ALL, "de_AT.UTF-8", nullptr, ENOENT, "archive no fallback");
    }
    setlocale(LC_ALL, "C");
}

// rpmatch(): YESEXPR then NOEXPR of LC_MESSAGES (regular expressions).
static void test_rpmatch()
{
    set_locpath("dir");
    static const struct {
        const char *loc, *s;
        int want;
    } cases[] = {
        { "C", "y", 1 }, { "C", "Yes", 1 }, { "C", "n", 0 }, { "C", "j", -1 }, { "C", "", -1 },
        { "C", " y", -1 }, { "C", "nein", 0 },
        { "de_DE.UTF-8", "j", 1 }, { "de_DE.UTF-8", "Ja", 1 }, { "de_DE.UTF-8", "1", 1 },
        { "de_DE.UTF-8", "+", 1 }, { "de_DE.UTF-8", "nein", 0 }, { "de_DE.UTF-8", "0", 0 },
        { "de_DE.UTF-8", "x", -1 },
        { "ru_RU.UTF-8", "\u0434\u0430", 1 }, { "ru_RU.UTF-8", "\u041d\u0435\u0442", 0 },
        { "ja_JP.UTF-8", "\u306f\u3044", 1 }, { "ja_JP.UTF-8", "\u3044\u3044\u3048", 0 },
        { "ja_JP.UTF-8", "\uff59", 1 }, { "tr_TR.UTF-8", "evet", 1 }, { "tr_TR.UTF-8", "hay\u0131r", 0 },
        { "fa_IR.UTF-8", "o", 0 },
    };
    // The whole locale: with LC_CTYPE C, glibc's regex matches bytes (a
    // UTF-8 letter then matches a bracket by its lead byte).
    for (auto &c : cases) {
        setlocale(LC_ALL, c.loc);
        int r = rpmatch(c.s);
        report(r == c.want, std::string("rpmatch ") + c.loc + " " + show(c.s) + " = " +
               std::to_string(r));
    }
    // Only LC_MESSAGES matters, and a change takes effect at once.
    setlocale(LC_ALL, "C");
    setlocale(LC_MESSAGES, "de_DE.UTF-8");
    int a = rpmatch("j");
    setlocale(LC_MESSAGES, "C");
    int b = rpmatch("j");
    report(a == 1 && b == -1, "rpmatch follows LC_MESSAGES");
    setlocale(LC_ALL, "C");
}

// strfmon builds the whole grouped number, or fails with E2BIG: never a
// truncated amount (glibc 2.41: fr_FR's separator is U+202F, 3 bytes, so
// 1e308's 309 integer digits and 102 separators, ",00" and " \u20ac" are 622
// bytes; with '!' 618).
static void test_strfmon()
{
    set_locpath("dir");
    setlocale(LC_ALL, "fr_FR.UTF-8");
    static char buf[8192];
    ssize_t r = strfmon(buf, sizeof buf, "%!n", 1e308);
    int digits = 0;
    for (const char *p = buf; r > 0 && *p; p++) {
        digits += *p >= '0' && *p <= '9';
    }
    report(r == 618 && digits == 311 && !strncmp(buf, "100\u202f000", 9),
           "fr_FR strfmon %!n 1e308: r " + std::to_string(r) + " digits " + std::to_string(digits));
    errno = 0;
    r = strfmon(buf, 618, "%!n", 1e308);
    report(r == -1 && errno == E2BIG, "fr_FR strfmon %!n 1e308 in 618 bytes: E2BIG, r " +
           std::to_string(r));
    r = strfmon(buf, 619, "%!n", 1e308);
    report(r == 618, "fr_FR strfmon %!n 1e308 in 619 bytes: r " + std::to_string(r));
    setlocale(LC_ALL, "C");
}

// Per-locale LC_CTYPE, expectations from glibc 2.41.
static void test_ctype()
{
    set_locpath("dir");
    setlocale(LC_ALL, "tr_TR.UTF-8");
    wctrans_t up = wctrans("toupper"), lo = wctrans("tolower");
    report(towupper('i') == 0x130 && towlower('I') == 0x131 && towctrans('i', up) == 0x130 &&
           towctrans(0x130, lo) == 'i' && towupper(0x131) == 'I',
           "tr_TR dotted and dotless i: towupper(i) " + std::to_string(towupper('i')));
    // Built with -fno-builtin-toupper -fno-builtin-tolower: GCC otherwise
    // assumes C's results (at -O1 it printed 105 and compared it != 'i').
    int ui = toupper('i'), lI = tolower('I'), ua = toupper('a');
    int ti = (*__ctype_toupper_loc())['i'];
    report(ui == 'i' && lI == 'I' && ua == 'A' && ti == 'i',
           "tr_TR byte toupper(i) = i (glibc): " + std::to_string(ui) + " " + std::to_string(lI) +
           " " + std::to_string(ua) + " " + std::to_string(ti));
    report(!isalpha(0xe9) && iswalpha(0xe9), "tr_TR byte ctype is C's, wide is Unicode");
    // The descriptor stays bound to the locale of the call.
    setlocale(LC_ALL, "C.UTF-8");
    report(towctrans('i', up) == 0x130 && towupper('i') == 'I', "wctrans bound at the call");
    setlocale(LC_ALL, "ja_JP.UTF-8");
    wctype_t jh = wctype("jhira"), jk = wctype("jkata"), jd = wctype("jdigit");
    wctrans_t tk = wctrans("tojkata"), th = wctrans("tojhira");
    report(jh && jk && jd && wctype("jkanji") && wctype("jspace") && iswctype(0x3042, jh) &&
           !iswctype(0x30a2, jh) && iswctype(0x30a2, jk) && iswctype(0xff11, jd) &&
           towctrans(0x3042, tk) == 0x30a2 && towctrans(0x30a2, th) == 0x3042,
           "ja_JP jhira jkata jdigit, tojkata tojhira");
    report(wcwidth(0x3042) == 2 && wcwidth('a') == 1 && wcwidth(0x110000) == -1,
           "ja_JP wcwidth");
    setlocale(LC_ALL, "C");
    report(!wctype("jhira") && !wctrans("tojkata") && wctype("alpha") && iswctype(0x3042, jh) &&
           wcwidth(0x3042) == -1, "C: no jhira; an old descriptor still works");
    locale_t tr = newlocale(LC_ALL_MASK, "tr_TR.UTF-8", nullptr);
    report(tr && towupper_l('i', tr) == 0x130 && towlower_l('I', tr) == 0x131 &&
           toupper_l('i', tr) == 'i' && toupper('i') == 'I', "towupper_l/toupper_l in tr_TR");
    if (tr) {
        locale_t old = uselocale(tr);
        report(towupper('i') == 0x130 && (*__ctype_toupper_loc())['i'] == 'i',
               "uselocale(tr_TR): towupper and __ctype_toupper_loc follow");
        uselocale(old);
        freelocale(tr);
    }
    report(towupper('i') == 'I', "back in C");
    // libstdc++'s std::ctype<char> copies the locale (duplocale) and reads
    // its byte tables from the object itself (glibc 2.41: 'i' stays 'i').
    try {
        std::locale cxx("tr_TR.UTF-8");
        const std::ctype<char> &ct = std::use_facet<std::ctype<char>>(cxx);
        char up = ct.toupper('i'), lo = ct.tolower('I'), ua = ct.toupper('a');
        report(up == 'i' && lo == 'I' && ua == 'A',
               std::string("std::ctype<char> tr_TR toupper(i) ") + up + " tolower(I) " + lo +
               " toupper(a) " + ua);
    } catch (const std::exception &e) {
        report(false, std::string("std::locale tr_TR.UTF-8: ") + e.what());
    }
    std::locale cxxc("C");
    report(std::use_facet<std::ctype<char>>(cxxc).toupper('i') == 'I', "std::ctype<char> C");
    setlocale(LC_ALL, "C");
}

static void test_composite_env()
{
    set_locpath("dir");
    setlocale(LC_ALL, "C");
    setlocale(LC_ALL, "de_DE.UTF-8");
    setlocale(LC_TIME, "fr_FR.UTF-8");
    setlocale(LC_MONETARY, "ja_JP.UTF-8");
    std::string all = setlocale(LC_ALL, nullptr);
    const char *want = "LC_CTYPE=de_DE.UTF-8;LC_NUMERIC=de_DE.UTF-8;LC_TIME=fr_FR.UTF-8;"
        "LC_COLLATE=de_DE.UTF-8;LC_MONETARY=ja_JP.UTF-8;LC_MESSAGES=de_DE.UTF-8;"
        "LC_PAPER=de_DE.UTF-8;LC_NAME=de_DE.UTF-8;LC_ADDRESS=de_DE.UTF-8;"
        "LC_TELEPHONE=de_DE.UTF-8;LC_MEASUREMENT=de_DE.UTF-8;LC_IDENTIFICATION=de_DE.UTF-8";
    report(all == want, "composite name " + show(all.c_str()));
    setlocale(LC_ALL, "C");
    const char *r = setlocale(LC_ALL, all.c_str());
    report(r && all == r && !strcmp(nl_langinfo(DAY_2), "lundi") &&
           !strcmp(localeconv()->currency_symbol, "\uffe5") &&
           !strcmp(localeconv()->decimal_point, ","),
           "setlocale(LC_ALL, composite) restores it: " + show(r));
    locale_t l = newlocale(LC_ALL_MASK, all.c_str(), nullptr);
    report(l && !strcmp(nl_langinfo_l(DAY_2, l), "lundi") &&
           !strcmp(nl_langinfo_l(_NL_ITEM(LC_MONETARY, 0xffff), l), "ja_JP.UTF-8"),
           "newlocale(composite)");
    if (l) {
        freelocale(l);
    }
    setlocale(LC_ALL, "C");
    struct { const char *vars[3][2]; const char *all; const char *time; } cases[] = {
        { {{ "LANG", "de_DE.UTF-8" }}, "de_DE.UTF-8", "de_DE.UTF-8" },
        { {{ "LANG", "de_DE.UTF-8" }, { "LC_TIME", "fr_FR.UTF-8" }}, "*", "fr_FR.UTF-8" },
        { {{ "LC_ALL", "ja_JP.UTF-8" }, { "LC_TIME", "fr_FR.UTF-8" }, { "LANG", "C" }}, "ja_JP.UTF-8",
          "ja_JP.UTF-8" },
        { {{ "LANG", "xx_YY.UTF-8" }}, nullptr, "C" },
        { {{ "LANG", "C" }, { "LC_TIME", "xx_YY.UTF-8" }}, nullptr, "C" },
    };
    for (auto &c : cases) {
        for (auto v : env_vars) {
            unsetenv(v);
        }
        std::string tag = "env";
        for (auto &kv : c.vars) {
            if (kv[0]) {
                setenv(kv[0], kv[1], 1);
                tag += std::string(" ") + kv[0] + "=" + kv[1];
            }
        }
        setlocale(LC_ALL, "C");
        const char *got = setlocale(LC_ALL, "");
        bool ok = !c.all ? !got : !strcmp(c.all, "*") ? got && !strncmp(got, "LC_CTYPE=", 9) :
            got && !strcmp(got, c.all);
        report(ok && !strcmp(setlocale(LC_TIME, nullptr), c.time),
               tag + ": " + show(got) + " LC_TIME " + show(setlocale(LC_TIME, nullptr)));
    }
    for (auto v : env_vars) {
        unsetenv(v);
    }
    setlocale(LC_ALL, "C");
}

static void test_newlocale_lifetimes()
{
    set_locpath("dir");
    locale_t de = newlocale(LC_ALL_MASK, "de_DE.UTF-8", nullptr);
    report(de && !strcmp(nl_langinfo_l(DAY_1, de), "Sonntag"), "newlocale de_DE");
    if (!de) {
        return;
    }
    // A category changed on a copy of a locale.
    locale_t mix = newlocale(LC_TIME_MASK, "fr_FR.UTF-8", duplocale(de));
    report(mix && !strcmp(nl_langinfo_l(DAY_1, mix), "dimanche") &&
           !strcmp(nl_langinfo_l(RADIXCHAR, mix), ",") &&
           !strcmp(nl_langinfo_l(DAY_1, de), "Sonntag"), "newlocale(LC_TIME_MASK, fr, dup(de))");
    // Use after freeing a copy: the original and other copies keep working
    // (the data is never unmapped).
    locale_t d2 = duplocale(de);
    freelocale(d2);
    locale_t d3 = duplocale(mix);
    freelocale(mix);
    report(!strcmp(nl_langinfo_l(DAY_1, de), "Sonntag") &&
           !strcmp(nl_langinfo_l(DAY_1, d3), "dimanche") &&
           !strcmp(nl_langinfo_l(MON_1, d3), "janvier"), "copies outlive freelocale of a copy");
    const char *day = nl_langinfo_l(DAY_1, de);
    freelocale(de);
    locale_t again = newlocale(LC_ALL_MASK, "de_DE.UTF-8", nullptr);
    report(again && nl_langinfo_l(DAY_1, again) == day && !strcmp(day, "Sonntag"),
           "a value stays valid after freelocale, and the data is cached");
    // uselocale: nl_langinfo and localeconv follow the thread's locale.
    setlocale(LC_ALL, "C");
    uselocale(again);
    report(!strcmp(nl_langinfo(DAY_1), "Sonntag") && !strcmp(localeconv()->decimal_point, ","),
           "uselocale(de_DE): nl_langinfo and localeconv");
    std::thread t([] {
        report(!strcmp(nl_langinfo(DAY_1), "Sunday") && !strcmp(localeconv()->decimal_point, "."),
               "another thread sees the global C");
    });
    t.join();
    uselocale(LC_GLOBAL_LOCALE);
    freelocale(again);
    freelocale(d3);
    // A failed newlocale leaves the base untouched.
    locale_t base = newlocale(LC_ALL_MASK, "it_IT.UTF-8", nullptr);
    errno = 0;
    report(base && !newlocale(LC_TIME_MASK, "xx_YY.UTF-8", base) && errno == ENOENT &&
           !strcmp(nl_langinfo_l(DAY_1, base), "domenica"), "failed newlocale keeps its base");
    if (base) {
        freelocale(base);
    }
    // Concurrent first loads of the same locale.
    std::thread th[8];
    bool ok[8] = {};
    for (int i = 0; i < 8; i++) {
        th[i] = std::thread([i, &ok] {
            locale_t l = newlocale(LC_ALL_MASK, "ko_KR.UTF-8", nullptr);
            ok[i] = l && !strcmp(nl_langinfo_l(CODESET, l), "UTF-8");
            if (l) {
                freelocale(l);
            }
        });
    }
    bool all = true;
    for (int i = 0; i < 8; i++) {
        th[i].join();
        all &= ok[i];
    }
    report(all, "8 threads newlocale ko_KR.UTF-8");
}

static void test_archive_default()
{
    // Unset LOCPATH: the archive, then /usr/lib/locale.
    set_locpath(nullptr);
    if (exists("/usr/lib/locale/locale-archive")) {
        setlocale(LC_ALL, "C");
        const char *r = setlocale(LC_ALL, "de_DE.UTF-8");
        if (!r) {
            skip("archive has no de_DE.utf8");
        } else {
            report(!strcmp(r, "de_DE.UTF-8") && !strcmp(nl_langinfo(DAY_1), "Sonntag"),
                   "archive de_DE.UTF-8");
            expect_set(LC_ALL, "ja_JP.utf8", "ja_JP.utf8", 0, "archive");
            expect_set(LC_ALL, "ja_JP.UTF+8", "ja_JP.UTF+8", 0, "archive");
            expect_set(LC_ALL, "de_DE.UTF-8@euro", nullptr, ENOENT, "archive");
            expect_set(LC_ALL, "de_DE.ISO-8859-1", nullptr, ENOENT, "archive");
            expect_set(LC_ALL, "xx_YY.UTF-8", nullptr, ENOENT, "archive");
        }
    } else {
        skip("no /usr/lib/locale/locale-archive");
    }
    if (exists("/usr/lib/locale/da_DK.utf8/LC_CTYPE")) {
        expect_set(LC_ALL, "da_DK.UTF-8", "da_DK.UTF-8", 0, "default dir");
        setlocale(LC_ALL, "da_DK.UTF-8");
        report(!strcmp(nl_langinfo(DAY_1), "s\u00f8ndag"), "default dir da_DK DAY_1 " +
               show(nl_langinfo(DAY_1)));
        // LOCPATH set: its directories, then still the default directory.
        set_locpath("dir");
        expect_set(LC_ALL, "da_DK.UTF-8", "da_DK.UTF-8", 0, "LOCPATH then default dir");
        set_locpath(nullptr);
    } else {
        skip("no /usr/lib/locale/da_DK.utf8");
    }
    setlocale(LC_ALL, "C");
}

int main()
{
    for (auto v : env_vars) {
        unsetenv(v);
    }
    const char *d = getenv("OSV_LOCALE_TEST_DATA");
    data_dir = d ? d : "/usr/share/osv-locale-test";
    bool have_data = exists(data_dir + "/dir/de_DE.utf8/LC_CTYPE");
    if (!OSV_CONF_LOCALE) {
        test_conf0();   // no data in the image; the names fail without it
    } else if (!have_data) {
        skip("no test locale data in " + data_dir);
    } else {
        test_locpath();
        test_fallback();
        test_rpmatch();
        test_strfmon();
        test_ctype();
        test_composite_env();
        test_newlocale_lifetimes();
        test_archive_default();
    }
    printf("SUMMARY: %d tests, %d failures, %d skipped\n", tests, fails, skips);
    return fails ? 1 : 0;
}
