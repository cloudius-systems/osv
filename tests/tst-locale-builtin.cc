/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// OSv's locales behave as glibc's do with the distribution's standard C.utf8
// installed (Debian/Ubuntu libc-bin, Fedora glibc) and no other locale.
//
// Every expectation below was taken from glibc 2.42, and this test passes
// there too (build it on Linux with g++ -std=c++17 and run it with LOCPATH
// pointing at an empty directory and LOCALE_ARCHIVE unset).
//
//   C and POSIX (reported as "C"), codeset ANSI_X3.4-1968;
//   C.<codeset>[@modifier] with a UTF-8 codeset, reported as requested,
//   codeset UTF-8; any other name fails with ENOENT, and a name with '/',
//   "..", or more than 255 bytes fails with EINVAL.

#include <cerrno>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <thread>

#include <langinfo.h>
#include <locale.h>

// glibc's nl_langinfo item numbers (its <langinfo.h> enum); not every libc
// header defines these, but they are part of the ABI glibc programs use.
#ifndef _NL_ITEM
#define _NL_ITEM(cat, idx) (((cat) << 16) | (idx))
#endif
enum {
    G_DECIMAL_POINT_WC = _NL_ITEM(LC_NUMERIC, 3),
    G_THOUSANDS_SEP_WC = _NL_ITEM(LC_NUMERIC, 4),
    G_NUMERIC_CODESET = _NL_ITEM(LC_NUMERIC, 5),
    G_WDAY_1 = _NL_ITEM(LC_TIME, 59),
    G_WD_T_FMT = _NL_ITEM(LC_TIME, 92),
    G_WEEK_NDAYS = _NL_ITEM(LC_TIME, 101),
    G_WEEK_1STDAY = _NL_ITEM(LC_TIME, 102),
    G_DATE_FMT = _NL_ITEM(LC_TIME, 108),
    G_TIME_CODESET = _NL_ITEM(LC_TIME, 110),
    G_FRAC_DIGITS = _NL_ITEM(LC_MONETARY, 8),
    G_CRNCYSTR = _NL_ITEM(LC_MONETARY, 15),
    G_MON_DECIMAL_POINT_WC = _NL_ITEM(LC_MONETARY, 43),
    G_MONETARY_CODESET = _NL_ITEM(LC_MONETARY, 45),
    G_PAPER_HEIGHT = _NL_ITEM(7, 0),
    G_PAPER_WIDTH = _NL_ITEM(7, 1),
    G_ADDRESS_POSTAL_FMT = _NL_ITEM(9, 0),
    G_MEASUREMENT = _NL_ITEM(11, 0),
    G_IDENTIFICATION_TITLE = _NL_ITEM(12, 0),
    G_MB_CUR_MAX = _NL_ITEM(LC_CTYPE, 13),
};

static int tests, fails;

static void report(bool ok, const std::string &what)
{
    tests++;
    if (!ok) {
        fails++;
        printf("FAIL: %s\n", what.c_str());
    }
}

static std::string show(const char *s)
{
    return s ? std::string("\"") + s + "\"" : "NULL";
}

// glibc's extra categories (not in every libc's <locale.h>).
enum { CAT_PAPER = 7, CAT_NAME, CAT_ADDRESS, CAT_TELEPHONE, CAT_MEASUREMENT,
       CAT_IDENTIFICATION, NCAT };
static const int glibc_all_mask = ((1 << NCAT) - 1) & ~(1 << LC_ALL);

static void test_names()
{
    struct { const char *name; const char *reported; int err; } cases[] = {
        { "C", "C", 0 },
        { "POSIX", "C", 0 },
        { "", "C", 0 },
        { "C.UTF-8", "C.UTF-8", 0 },
        { "C.utf8", "C.utf8", 0 },
        { "C.UTF8", "C.UTF8", 0 },
        { "C.utf-8", "C.utf-8", 0 },
        { "C.UTF+8", "C.UTF+8", 0 },
        { "C.UTF-8@euro", "C.UTF-8@euro", 0 },
        { "C.utf8@x@y", "C.utf8@x@y", 0 },
        { "C.UTF-8@", "C.UTF-8@", 0 },
        { "C_US.UTF-8", "C_US.UTF-8", 0 },
        { "C_.utf8", "C_.utf8", 0 },
        { "C_x_y.utf8", "C_x_y.utf8", 0 },
        { "c.utf8", nullptr, ENOENT },
        { "POSIX.UTF-8", nullptr, ENOENT },
        { "C.", nullptr, ENOENT },
        { "C@x", nullptr, ENOENT },
        { "C_US", nullptr, ENOENT },
        { "C.ascii", nullptr, ENOENT },
        { "C.ISO-8859-1", nullptr, ENOENT },
        { "C.U-T-F-8", nullptr, ENOENT },
        { "C.utf_8", nullptr, ENOENT },
        { "C.UTF-8.", nullptr, ENOENT },
        { "C.UTF:8", nullptr, ENOENT },
        { "C..utf8", nullptr, ENOENT },
        { " C", nullptr, ENOENT },
        { "en_US.UTF-8", nullptr, ENOENT },
        { "de_DE.UTF-8", nullptr, ENOENT },
        { "xx_YY.UTF-8", nullptr, ENOENT },
        { "UTF-8", nullptr, ENOENT },
        { "..", nullptr, EINVAL },
        { "C.utf8/", nullptr, EINVAL },
        { "../C.utf8", nullptr, EINVAL },
        { "C.UTF-8@a/b", nullptr, EINVAL },
    };
    for (auto &c : cases) {
        std::string tag = std::string("name ") + show(c.name);
        errno = 0;
        locale_t l = newlocale(LC_ALL_MASK, c.name, nullptr);
        int err = errno;
        report(c.reported ? l != nullptr : (l == nullptr && err == c.err),
               tag + ": newlocale " + (l ? "ok" : "NULL errno " + std::to_string(err)));
        if (l && c.reported) {
            report(!strcmp(nl_langinfo_l(_NL_LOCALE_NAME(LC_CTYPE), l), c.reported),
                   tag + ": newlocale name " + show(nl_langinfo_l(_NL_LOCALE_NAME(LC_CTYPE), l)));
            freelocale(l);
        }
        setlocale(LC_ALL, "C");
        errno = 0;
        const char *r = setlocale(LC_CTYPE, c.name);
        err = errno;
        report(c.reported ? (r && !strcmp(r, c.reported)) : (!r && err == c.err),
               tag + ": setlocale gives " + show(r) + " errno " + std::to_string(err));
    }

    // Length limit: 255 bytes is accepted, 256 is EINVAL.
    std::string n255 = "C.UTF-8@" + std::string(255 - 8, 'a');
    std::string n256 = "C.UTF-8@" + std::string(256 - 8, 'a');
    locale_t l = newlocale(LC_ALL_MASK, n255.c_str(), nullptr);
    report(l != nullptr, "255-byte name accepted");
    if (l) {
        freelocale(l);
    }
    errno = 0;
    report(!newlocale(LC_ALL_MASK, n256.c_str(), nullptr) && errno == EINVAL,
           "256-byte name is EINVAL");
}

static void test_codeset()
{
    setlocale(LC_ALL, "C");
    report(!strcmp(nl_langinfo(CODESET), "ANSI_X3.4-1968"),
           "C codeset " + show(nl_langinfo(CODESET)));
    setlocale(LC_CTYPE, "C.utf8");
    report(!strcmp(nl_langinfo(CODESET), "UTF-8"),
           "C.utf8 codeset " + show(nl_langinfo(CODESET)));
    // A per-category codeset follows that category's locale.
    report(!strcmp(nl_langinfo(G_TIME_CODESET), "ANSI_X3.4-1968"),
           "LC_TIME still C: " + show(nl_langinfo(G_TIME_CODESET)));
    setlocale(LC_TIME, "C.UTF-8");
    report(!strcmp(nl_langinfo(G_TIME_CODESET), "UTF-8"),
           "LC_TIME C.UTF-8: " + show(nl_langinfo(G_TIME_CODESET)));
    setlocale(LC_ALL, "C");
}

static void test_composite()
{
    const char *mixed = "LC_CTYPE=C.UTF-8;LC_NUMERIC=C;LC_TIME=C;LC_COLLATE=C;"
        "LC_MONETARY=C;LC_MESSAGES=C;LC_PAPER=C;LC_NAME=C;LC_ADDRESS=C;"
        "LC_TELEPHONE=C;LC_MEASUREMENT=C;LC_IDENTIFICATION=C";
    setlocale(LC_ALL, "C");
    setlocale(LC_CTYPE, "C.UTF-8");
    const char *q = setlocale(LC_ALL, nullptr);
    report(q && !strcmp(q, mixed), "mixed setlocale(LC_ALL, NULL) " + show(q));
    std::string saved = q ? q : "";
    setlocale(LC_ALL, "C");
    report(!strcmp(setlocale(LC_ALL, nullptr), "C"), "all C reports \"C\"");
    const char *r = setlocale(LC_ALL, saved.c_str());
    report(r && !strcmp(r, mixed), "composite restores " + show(r));
    report(!strcmp(setlocale(LC_CTYPE, nullptr), "C.UTF-8"), "restored LC_CTYPE");

    // Order does not matter, a trailing ';' is allowed, the last repeat wins.
    const char *reordered = "LC_NUMERIC=C;LC_CTYPE=C.UTF-8;LC_TIME=C;LC_COLLATE=C;"
        "LC_MONETARY=C;LC_MESSAGES=C;LC_PAPER=C;LC_NAME=C;LC_ADDRESS=C;"
        "LC_TELEPHONE=C;LC_MEASUREMENT=C;LC_IDENTIFICATION=C;";
    setlocale(LC_ALL, "C");
    r = setlocale(LC_ALL, reordered);
    report(r && !strcmp(r, mixed), "reordered composite " + show(r));

    // Every category must be named; unknown categories and LC_ALL are EINVAL;
    // a bad value is ENOENT; nothing changes on failure.
    struct { const char *s; int err; } bad[] = {
        { "LC_CTYPE=C.UTF-8;LC_NUMERIC=C", EINVAL },
        { "LC_CTYPE=C.UTF-8;LC_FOO=C;LC_TIME=C;LC_COLLATE=C;LC_MONETARY=C;"
          "LC_MESSAGES=C;LC_PAPER=C;LC_NAME=C;LC_ADDRESS=C;LC_TELEPHONE=C;"
          "LC_MEASUREMENT=C;LC_IDENTIFICATION=C", EINVAL },
        { "LC_CTYPE=xx_YY;LC_NUMERIC=C;LC_TIME=C;LC_COLLATE=C;LC_MONETARY=C;"
          "LC_MESSAGES=C;LC_PAPER=C;LC_NAME=C;LC_ADDRESS=C;LC_TELEPHONE=C;"
          "LC_MEASUREMENT=C;LC_IDENTIFICATION=C", ENOENT },
        { "LC_ALL=C;LC_CTYPE=C.UTF-8;LC_NUMERIC=C;LC_TIME=C;LC_COLLATE=C;"
          "LC_MONETARY=C;LC_MESSAGES=C;LC_PAPER=C;LC_NAME=C;LC_ADDRESS=C;"
          "LC_TELEPHONE=C;LC_MEASUREMENT=C;LC_IDENTIFICATION=C", EINVAL },
    };
    for (auto &b : bad) {
        setlocale(LC_ALL, "C");
        setlocale(LC_TIME, "C.utf8");
        errno = 0;
        r = setlocale(LC_ALL, b.s);
        int err = errno;
        report(!r && err == b.err, std::string("bad composite ") + b.s +
               " errno " + std::to_string(err));
        report(!strcmp(setlocale(LC_TIME, nullptr), "C.utf8") &&
               !strcmp(setlocale(LC_CTYPE, nullptr), "C"),
               "failed composite changed nothing");
        errno = 0;
        report(!newlocale(LC_ALL_MASK, b.s, nullptr) && errno == b.err,
               std::string("newlocale bad composite errno ") + std::to_string(errno));
    }
    // A composite is only for LC_ALL.
    errno = 0;
    report(!setlocale(LC_CTYPE, mixed) && errno == ENOENT, "composite for one category is ENOENT");
    locale_t l = newlocale(LC_ALL_MASK, mixed, nullptr);
    report(l && !strcmp(nl_langinfo_l(CODESET, l), "UTF-8") &&
           !strcmp(nl_langinfo_l(_NL_LOCALE_NAME(LC_NUMERIC), l), "C"),
           "newlocale with a composite");
    if (l) {
        freelocale(l);
    }
    setlocale(LC_ALL, "C");
}

static void test_environment()
{
    struct { const char *vars[3][2]; const char *all; const char *ctype; const char *numeric; } cases[] = {
        { {{ "LANG", "C.UTF-8" }}, "C.UTF-8", "C.UTF-8", "C.UTF-8" },
        { {{ "LC_ALL", "C.UTF-8" }}, "C.UTF-8", "C.UTF-8", "C.UTF-8" },
        { {{ "LANG", "C.UTF-8" }, { "LC_NUMERIC", "C" }}, "*", "C.UTF-8", "C" },
        { {{ "LC_ALL", "POSIX" }, { "LANG", "C.UTF-8" }}, "C", "C", "C" },
        { {{ "LANG", "de_DE.UTF-8" }}, nullptr, "C", "C" },
        { {{ "LANG", "" }, { "LC_ALL", "" }}, "C", "C", "C" },
        { {{ nullptr, nullptr }}, "C", "C", "C" },
    };
    const char *vars[] = { "LC_ALL", "LANG", "LC_CTYPE", "LC_NUMERIC", "LC_TIME",
        "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES", "LC_PAPER", "LC_NAME",
        "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION" };
    for (auto &c : cases) {
        for (auto v : vars) {
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
        const char *r = setlocale(LC_ALL, "");
        // c.all is the expected result: a name, "*" for a composite, or
        // nullptr when the environment names a locale that is not available.
        bool ok = !c.all ? !r : !strcmp(c.all, "*") ? (r && !strncmp(r, "LC_CTYPE=", 9)) :
            (r && !strcmp(r, c.all));
        report(ok, tag + ": setlocale(LC_ALL, \"\") " + show(r));
        report(!strcmp(setlocale(LC_CTYPE, nullptr), c.ctype) &&
               !strcmp(setlocale(LC_NUMERIC, nullptr), c.numeric),
               tag + ": LC_CTYPE " + show(setlocale(LC_CTYPE, nullptr)) +
               " LC_NUMERIC " + show(setlocale(LC_NUMERIC, nullptr)));
    }
    for (auto v : vars) {
        unsetenv(v);
    }
    setlocale(LC_ALL, "C");
}

static void test_newlocale_api()
{
    locale_t c = newlocale(LC_ALL_MASK, "C", nullptr);
    report(c && c == newlocale(LC_ALL_MASK, "C", nullptr), "C is one shared object");
    report(duplocale(c) == c, "duplocale(C) is C");
    errno = 0;
    report(!newlocale(1 << 13, "C", nullptr) && errno == EINVAL, "mask bit 13 is EINVAL");
    errno = 0;
    report(!newlocale(LC_ALL_MASK, nullptr, nullptr) && errno == EINVAL, "NULL name is EINVAL");
    locale_t m = newlocale(1 << LC_ALL, "C.UTF-8", nullptr);
    report(m && !strcmp(nl_langinfo_l(CODESET, m), "UTF-8"), "mask 1 << LC_ALL means every category");
    if (m) {
        freelocale(m);
    }
    locale_t z = newlocale(0, "garbage", nullptr);
    report(z != nullptr, "mask 0 ignores the name");
    if (z && z != c) {
        freelocale(z);
    }
    // A base is updated and the C object is never modified.
    if (!c) {
        report(false, "newlocale(C) failed");
        return;
    }
    locale_t u = newlocale(LC_CTYPE_MASK, "C.UTF-8", c);
    report(u && u != c && !strcmp(nl_langinfo_l(CODESET, c), "ANSI_X3.4-1968"),
           "newlocale with the C object as base copies it");
    if (!u) {
        return;
    }
    locale_t u2 = newlocale(LC_NUMERIC_MASK, "C.utf8", u);
    if (!u2) {
        report(false, "newlocale(C.utf8, base) failed");
        return;
    }
    report(u2 && !strcmp(nl_langinfo_l(_NL_LOCALE_NAME(LC_NUMERIC), u2), "C.utf8") &&
           !strcmp(nl_langinfo_l(_NL_LOCALE_NAME(LC_CTYPE), u2), "C.UTF-8"),
           "newlocale keeps the base's other categories");
    errno = 0;
    report(!newlocale(LC_NUMERIC_MASK, "xx", u2) && errno == ENOENT &&
           !strcmp(nl_langinfo_l(_NL_LOCALE_NAME(LC_CTYPE), u2), "C.UTF-8"),
           "failed newlocale leaves the base usable");
    locale_t d = duplocale(u2);
    report(d && !strcmp(nl_langinfo_l(_NL_LOCALE_NAME(LC_NUMERIC), d), "C.utf8"), "duplocale copies names");
    if (d) {
        freelocale(d);
    }
    freelocale(u2);

    // glibc's own extra categories work through setlocale too.
    setlocale(LC_ALL, "C");
    const char *p = setlocale(CAT_PAPER, "C.UTF-8");
    report(p && !strcmp(p, "C.UTF-8"), "setlocale(LC_PAPER) " + show(p));
    errno = 0;
    report(!setlocale(NCAT, "C"), "category 13 rejected");
    report(!setlocale(-1, "C"), "category -1 rejected");
    setlocale(LC_ALL, "C");
}

static void test_uselocale()
{
    report(uselocale(nullptr) == LC_GLOBAL_LOCALE, "a thread starts on the global locale");
    setlocale(LC_ALL, "C");
    locale_t u = newlocale(LC_ALL_MASK, "C.UTF-8", nullptr);
    if (!u) {
        report(false, "newlocale(C.UTF-8) failed");
        return;
    }
    locale_t old = uselocale(u);
    report(old == LC_GLOBAL_LOCALE, "uselocale returns LC_GLOBAL_LOCALE");
    report(!strcmp(nl_langinfo(CODESET), "UTF-8"), "nl_langinfo follows the thread locale");
    // Another thread still sees the global locale.
    std::string other;
    std::thread t([&] { other = nl_langinfo(CODESET); });
    t.join();
    report(other == "ANSI_X3.4-1968", "other thread sees the global locale: " + other);
    report(uselocale(LC_GLOBAL_LOCALE) == u, "uselocale returns the previous locale");
    report(!strcmp(nl_langinfo(CODESET), "ANSI_X3.4-1968"), "back on the global locale");
    locale_t g = duplocale(LC_GLOBAL_LOCALE);
    report(g && !strcmp(nl_langinfo_l(CODESET, g), "ANSI_X3.4-1968"), "duplocale(LC_GLOBAL_LOCALE)");
    if (g) {
        freelocale(g);
    }
    freelocale(u);
}

static void test_langinfo()
{
    locale_t c = newlocale(LC_ALL_MASK, "C", nullptr);
    locale_t u = newlocale(LC_ALL_MASK, "C.UTF-8", nullptr);
    if (!c || !u) {
        report(false, "newlocale(C / C.UTF-8) failed");
        return;
    }
    struct { nl_item item; const char *c; const char *u; } strs[] = {
        { RADIXCHAR, ".", "." },
        { THOUSEP, "", "" },
        { ABDAY_1, "Sun", "Sun" },
        { DAY_7, "Saturday", "Saturday" },
        { ABMON_12, "Dec", "Dec" },
        { MON_1, "January", "January" },
        { AM_STR, "AM", "AM" },
        { D_T_FMT, "%a %b %e %H:%M:%S %Y", "%a %b %e %H:%M:%S %Y" },
        { D_FMT, "%m/%d/%y", "%m/%d/%y" },
        { T_FMT, "%H:%M:%S", "%H:%M:%S" },
        { T_FMT_AMPM, "%I:%M:%S %p", "%I:%M:%S %p" },
        { G_DATE_FMT, "%a %b %e %H:%M:%S %Z %Y", "%a %b %e %H:%M:%S %Z %Y" },
        { YESEXPR, "^[yY]", "^[yY]" },
        { NOEXPR, "^[nN]", "^[nN]" },
        { G_CRNCYSTR, "-", "-" },
        { G_NUMERIC_CODESET, "ANSI_X3.4-1968", "UTF-8" },
        { G_MONETARY_CODESET, "ANSI_X3.4-1968", "UTF-8" },
        { G_IDENTIFICATION_TITLE, "ISO/IEC 14652 i18n FDCC-set", "C locale" },
        { G_ADDRESS_POSTAL_FMT, "%a%N%f%N%d%N%b%N%s %h %e %r%N%C-%z %T%N%c%N",
          "%a%N%f%N%d%N%b%N%s %h %e %r%N%C-%z %T%N%c%N" },
        { _NL_ITEM(LC_TIME, 500), "", "" },
        { _NL_ITEM(NCAT, 0), "", "" },
    };
    for (auto &s : strs) {
        report(!strcmp(nl_langinfo_l(s.item, c), s.c),
               "C item " + std::to_string(s.item) + " " + show(nl_langinfo_l(s.item, c)));
        report(!strcmp(nl_langinfo_l(s.item, u), s.u),
               "C.UTF-8 item " + std::to_string(s.item) + " " + show(nl_langinfo_l(s.item, u)));
    }
    // Word items carry the value in the pointer bits, as libstdc++ reads them.
    auto word = [](nl_item i, locale_t l) { return uint32_t(uintptr_t(nl_langinfo_l(i, l))); };
    report(word(G_DECIMAL_POINT_WC, c) == '.', "wide decimal point");
    report(word(G_THOUSANDS_SEP_WC, c) == 0, "wide thousands separator");
    report(word(G_MON_DECIMAL_POINT_WC, u) == 0, "wide monetary decimal point");
    report(word(G_PAPER_HEIGHT, c) == 297 && word(G_PAPER_WIDTH, c) == 210, "A4 paper");
    report(word(G_WEEK_1STDAY, c) == 19971130, "week first day");
    report(word(G_MB_CUR_MAX, c) == 1, "C MB_CUR_MAX item is 1");
    // Byte items: CHAR_MAX for the monetary formatting flags.
    report((unsigned char)nl_langinfo_l(G_FRAC_DIGITS, c)[0] == 0xff, "frac digits CHAR_MAX");
    report(nl_langinfo_l(G_MEASUREMENT, c)[0] == 1, "metric");
    report(nl_langinfo_l(G_WEEK_NDAYS, c)[0] == 7, "seven-day week");
    // Wide strings.
    const wchar_t *wday = reinterpret_cast<const wchar_t *>(nl_langinfo_l(G_WDAY_1, u));
    report(!wcscmp(wday, L"Sunday"), "wide day name");
    const wchar_t *wfmt = reinterpret_cast<const wchar_t *>(nl_langinfo_l(G_WD_T_FMT, c));
    report(!wcscmp(wfmt, L"%a %b %e %H:%M:%S %Y"), "wide date-time format");
    freelocale(u);
}

static void test_cxx()
{
    // A named C++ locale now constructs; an unknown one still throws.
    bool threw = false;
    try {
        std::locale bogus("NonexistentLocale");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    report(threw, "std::locale of an unknown name throws");
    std::locale l("C.UTF-8");
    report(l.name() == "C.UTF-8", "std::locale name " + l.name());
    std::ostringstream o;
    o.imbue(l);
    o << 1234.5 << ' ' << 42;
    report(o.str() == "1234.5 42", "narrow numbers: " + o.str());
    std::wostringstream w;
    w.imbue(l);
    w << 1234.5;
    report(w.str() == L"1234.5", "wide decimal point");
    std::tm tm{};
    tm.tm_year = 126;
    tm.tm_mon = 2;
    tm.tm_mday = 1;
    std::ostringstream t;
    t.imbue(l);
    t << std::put_time(&tm, "%c|%A|%B|%p");
    report(t.str() == "Sun Mar  1 00:00:00 2026|Sunday|March|AM", "put_time: " + t.str());
    std::wostringstream wt;
    wt.imbue(l);
    wt << std::put_time(&tm, L"%B");
    report(wt.str() == L"March", "wide put_time");
    std::istringstream is("Mar 1 2026");
    is.imbue(l);
    std::tm parsed{};
    is >> std::get_time(&parsed, "%b %d %Y");
    report(!is.fail() && parsed.tm_mon == 2 && parsed.tm_mday == 1, "get_time");
}

int main()
{
    // The "" name cases expect C: start from an environment that names no
    // locale (many Linux hosts set LANG=C.UTF-8).
    for (const char *v : { "LC_ALL", "LANG", "LANGUAGE", "LC_CTYPE", "LC_NUMERIC", "LC_TIME",
                           "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES", "LC_PAPER", "LC_NAME",
                           "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION" }) {
        unsetenv(v);
    }
    test_names();
    test_codeset();
    test_composite();
    test_environment();
    test_newlocale_api();
    test_uselocale();
    test_langinfo();
    test_cxx();
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
