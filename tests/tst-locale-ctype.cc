/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Multibyte conversion and MB_CUR_MAX in the C and C.UTF-8 locales, as glibc
// gives them with the distribution's standard C.utf8 installed (Debian/Ubuntu
// libc-bin, Fedora glibc). The expectations were measured on glibc 2.41, and
// this test passes there too (g++ -std=c++17, default environment).
//
// C: bytes 0..0x7f are characters, any other byte is EILSEQ, MB_CUR_MAX is 1,
// and regex and fnmatch take each high byte as one character.
// C.UTF-8: glibc's UTF-8, including the legacy 5- and 6-byte forms up to
// 0x7fffffff when decoding; MB_CUR_MAX is 6.
//
// One deliberate difference: glibc's wcrtomb() encodes 0x200000..0x7fffffff
// as 5 or 6 bytes, while OSv's fails with EILSEQ, because musl's stdio
// converts into 4-byte (MB_LEN_MAX) buffers and such values are never
// Unicode. Those checks accept either answer.
//
// Wide classes, case maps and widths (B2): C has the C.UTF-8 answer below
// 0x80, and above it no class, the identity map and width -1. The expected
// values for a fixed sample of code points are glibc's, generated into
// tst-locale-ctype-sample.h by scripts/locale-oracle/gen-ctype-sample.py.
// wctype() and wctrans() descriptors are bound to the LC_CTYPE current at
// the call; iswctype_l() and towctrans_l() ignore their locale argument.
//
// Wide stdio (B3), one check per row of the glibc oracle (wio.c, p3.c) in C
// and C.UTF-8: byte-backed wide output writes the C transliteration or '?'
// and succeeds; byte-backed wide input of an invalid sequence is WEOF with
// EILSEQ and an error, its bytes pushed back; swprintf, swscanf and
// open_wmemstream never convert. Known differences (rows not checked here,
// see libc/locale/README.md): ftell() after ungetwc(); fputwc(WEOF) writes
// '?' and returns WEOF (glibc: nothing, 0); wide calls on a byte-oriented
// stream (fgetwc, fputwc, fputws, fwprintf) work instead of failing; on a
// stream bound to C while LC_CTYPE is C.UTF-8, the C transliteration is used
// (glibc: the current locale's, so 0xe9 is 'e'); fgetws leaves errno EAGAIN
// on success.

#include <cerrno>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>

#include <fnmatch.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#include <regex.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wctype.h>

#include "tst-locale-ctype-sample.h"

static int tests, fails;

static void report(bool ok, const std::string &what)
{
    tests++;
    if (!ok) {
        fails++;
        printf("FAIL: %s\n", what.c_str());
    }
}

static std::string hex(const char *s, size_t n)
{
    std::string r;
    char b[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(b, sizeof(b), "%02x", (unsigned char)s[i]);
        r += b;
    }
    return r;
}

static size_t mb_cur_max_item()
{
    // A word item in the pointer bits; glibc reading from a locale archive
    // leaves unrelated bytes in the upper half, so compare the 32-bit word.
    return uint32_t(uintptr_t(nl_langinfo((LC_CTYPE << 16) | 13)));    // _NL_CTYPE_MB_CUR_MAX
}

// mbrtowc from the initial state: the return value, the character and errno.
struct conv {
    size_t r;
    wchar_t wc;
    int err;
};

static conv mbr(const char *s, size_t n)
{
    mbstate_t st{};
    wchar_t wc = L'#';
    errno = 0;
    size_t r = mbrtowc(&wc, s, n, &st);
    return { r, wc, errno };
}

static void expect_mbr(const char *what, const char *s, size_t n, size_t r, wchar_t wc = L'#',
                       int err = 0)
{
    conv c = mbr(s, n);
    bool ok = c.r == r && c.err == err && (r >= size_t(-2) || c.wc == wc);
    report(ok, std::string(what) + ": mbrtowc(" + hex(s, n) + ") = " +
           std::to_string(long(c.r)) + " wc " + std::to_string(long(c.wc)) +
           " errno " + std::to_string(c.err));
}

static size_t wcr(wchar_t wc, char *buf, int *err)
{
    mbstate_t st{};
    errno = 0;
    size_t r = wcrtomb(buf, wc, &st);
    *err = errno;
    return r;
}

static bool regex_match(const char *re, const char *s)
{
    regex_t r;
    if (regcomp(&r, re, REG_NOSUB)) {
        return false;
    }
    bool m = regexec(&r, s, 0, nullptr, 0) == 0;
    regfree(&r);
    return m;
}

static void test_c()
{
    setlocale(LC_ALL, "C");
    const char *tag = "C";
    report(MB_CUR_MAX == 1, "C MB_CUR_MAX " + std::to_string(MB_CUR_MAX));
    report(mb_cur_max_item() == 1, "C _NL_CTYPE_MB_CUR_MAX");

    expect_mbr(tag, "A", 1, 1, L'A');
    expect_mbr(tag, "\x7f", 1, 1, 0x7f);
    expect_mbr(tag, "", 1, 0, 0);
    expect_mbr(tag, "\x80", 1, size_t(-1), 0, EILSEQ);
    expect_mbr(tag, "\xc3\xa9", 2, size_t(-1), 0, EILSEQ);
    expect_mbr(tag, "\xff", 1, size_t(-1), 0, EILSEQ);
    expect_mbr(tag, "A", 0, size_t(-2));
    mbstate_t st{};
    report(mbrtowc(nullptr, nullptr, 0, &st) == 0 && mbsinit(&st), "C mbrtowc(NULL) resets");

    wchar_t wc = 0;
    report(mbtowc(nullptr, nullptr, 0) == 0, "C mbtowc(NULL) is 0");
    report(mbtowc(&wc, "a", 1) == 1 && wc == L'a', "C mbtowc a");
    errno = 0;
    report(mbtowc(&wc, "\xe9", 1) == -1 && errno == EILSEQ, "C mbtowc high byte EILSEQ");

    // mbsrtowcs stops at the high byte; counting leaves *src alone.
    const char *in = "ab\xe9" "c";
    const char *src = in;
    wchar_t out[8];
    st = {};
    errno = 0;
    size_t r = mbsrtowcs(out, &src, 8, &st);
    report(r == size_t(-1) && errno == EILSEQ && src == in + 2, "C mbsrtowcs stops at the high byte");
    src = in;
    errno = 0;
    r = mbsrtowcs(nullptr, &src, 0, &st);
    report(r == size_t(-1) && errno == EILSEQ && src == in, "C mbsrtowcs count");
    src = "abc";
    r = mbsrtowcs(out, &src, 8, &st);
    report(r == 3 && !src && !wcscmp(out, L"abc"), "C mbsrtowcs abc");
    const char *abc = "abc";
    src = abc;
    r = mbsrtowcs(out, &src, 2, &st);
    report(r == 2 && src == abc + 2, "C mbsrtowcs len 2");
    src = in;
    errno = 0;
    r = mbsnrtowcs(out, &src, 4, 8, &st);
    report(r == size_t(-1) && errno == EILSEQ && src == in + 2, "C mbsnrtowcs stops at the high byte");

    report(btowc('a') == L'a' && btowc(0x7f) == 0x7f, "C btowc ASCII");
    report(btowc(0x80) == WEOF && btowc(0xff) == WEOF && btowc(EOF) == WEOF &&
           btowc(-2) == WEOF && btowc(0x100) == WEOF, "C btowc non-ASCII is WEOF");
    report(wctob(L'a') == 'a' && wctob(0x80) == EOF && wctob(0xe9) == EOF, "C wctob");

    char buf[8];
    int err;
    report(wcr(L'a', buf, &err) == 1 && buf[0] == 'a', "C wcrtomb a");
    report(wcr(0x80, buf, &err) == size_t(-1) && err == EILSEQ, "C wcrtomb 0x80 EILSEQ");
    report(wcr(0xe9, buf, &err) == size_t(-1) && err == EILSEQ, "C wcrtomb 0xe9 EILSEQ");
    report(wcrtomb(nullptr, 0xe9, &st) == 1, "C wcrtomb(NULL) is 1");
    const wchar_t *ws = L"a\xe9";
    errno = 0;
    report(wcsrtombs(nullptr, &ws, 0, nullptr) == size_t(-1) && errno == EILSEQ,
           "C wcsrtombs(NULL) EILSEQ");

    // musl's callers of the six.
    report(mblen("\xe9", 1) == -1 && mblen("a", 1) == 1, "C mblen");
    report(wctomb(buf, 0xe9) == -1 && wctomb(buf, L'a') == 1, "C wctomb");
    report(mbstowcs(out, "a\xe9", 8) == size_t(-1), "C mbstowcs");

    // Each high byte is one character to regex and fnmatch.
    report(regex_match("a.b", "a\xe9" "b"), "C regex a.b matches a\\xe9b");
    report(regex_match("^..$", "\xc3\xa9"), "C regex ^..$ matches two bytes");
    report(!regex_match("^.$", "\xc3\xa9"), "C regex ^.$ does not match two bytes");
    report(regex_match("\xe9", "x\xe9"), "C regex high byte literal");
    report(fnmatch("?", "\xc3\xa9", 0) == FNM_NOMATCH, "C fnmatch ? two bytes NOMATCH");
    report(fnmatch("??", "\xc3\xa9", 0) == 0, "C fnmatch ?? two bytes");
    report(fnmatch("a*b", "a\xe9\xe9" "b", 0) == 0, "C fnmatch a*b");
    report(fnmatch("*\xe9", "ab\xe9", 0) == 0, "C fnmatch *\\xe9");
}

static void test_utf8()
{
    if (!setlocale(LC_ALL, "C.UTF-8")) {
        report(false, "setlocale(C.UTF-8)");
        return;
    }
    const char *tag = "UTF-8";
    report(MB_CUR_MAX == 6, "UTF-8 MB_CUR_MAX " + std::to_string(MB_CUR_MAX));
    report(mb_cur_max_item() == 6, "UTF-8 _NL_CTYPE_MB_CUR_MAX");

    expect_mbr(tag, "A", 1, 1, L'A');
    expect_mbr(tag, "", 1, 0, 0);
    expect_mbr(tag, "\xc2\x80", 2, 2, 0x80);
    expect_mbr(tag, "\xc3\xa9", 2, 2, 0xe9);
    expect_mbr(tag, "\xdf\xbf", 2, 2, 0x7ff);
    expect_mbr(tag, "\xe0\xa0\x80", 3, 3, 0x800);
    expect_mbr(tag, "\xe2\x82\xac", 3, 3, 0x20ac);
    expect_mbr(tag, "\xed\x9f\xbf", 3, 3, 0xd7ff);
    expect_mbr(tag, "\xee\x80\x80", 3, 3, 0xe000);
    expect_mbr(tag, "\xef\xbf\xbf", 3, 3, 0xffff);
    expect_mbr(tag, "\xf0\x90\x80\x80", 4, 4, 0x10000);
    expect_mbr(tag, "\xf0\x9f\x98\x80", 4, 4, 0x1f600);
    expect_mbr(tag, "\xf4\x8f\xbf\xbf", 4, 4, 0x10ffff);
    expect_mbr(tag, "\xf4\x90\x80\x80", 4, 4, 0x110000);
    expect_mbr(tag, "\xf7\xbf\xbf\xbf", 4, 4, 0x1fffff);
    expect_mbr(tag, "\xf8\x88\x80\x80\x80", 5, 5, 0x200000);
    expect_mbr(tag, "\xfb\xbf\xbf\xbf\xbf", 5, 5, 0x3ffffff);
    expect_mbr(tag, "\xfc\x84\x80\x80\x80\x80", 6, 6, 0x4000000);
    expect_mbr(tag, "\xfd\xbf\xbf\xbf\xbf\xbf", 6, 6, 0x7fffffff);
    // Bytes that cannot start a character.
    for (const char *s : { "\x80\x80", "\xbf\x80", "\xc0\x80", "\xc1\xbf", "\xfe\x80", "\xff\x80" }) {
        expect_mbr(tag, s, 2, size_t(-1), 0, EILSEQ);
    }
    // Overlong forms and surrogates fail at the byte that completes them,
    // not before (glibc 2.41: e0 80 80 byte by byte is -2, -2, -1).
    for (const char *s : { "\xe0\x80\x80", "\xe0\x9f\xbf", "\xed\xa0\x80", "\xed\xbf\xbf",
                           "\xf0\x80\x80\x80", "\xf0\x8f\xbf\xbf", "\xf8\x80\x80\x80\x80",
                           "\xf8\x87\xbf\xbf\xbf", "\xfc\x80\x80\x80\x80\x80",
                           "\xfc\x83\xbf\xbf\xbf\xbf" }) {
        size_t n = strlen(s);
        expect_mbr(tag, s, n, size_t(-1), 0, EILSEQ);
        expect_mbr(tag, s, n - 1, size_t(-2));
        mbstate_t bst{};
        wchar_t bwc;
        bool ok = true;
        for (size_t i = 0; i < n; i++) {
            errno = 0;
            size_t r = mbrtowc(&bwc, s + i, 1, &bst);
            ok = ok && (i + 1 < n ? r == size_t(-2) : (r == size_t(-1) && errno == EILSEQ));
        }
        report(ok, std::string("UTF-8 bytewise ") + hex(s, n) + " is -2 until the last byte");
    }
    expect_mbr(tag, "\xe0\x80\x41", 3, size_t(-1), 0, EILSEQ);
    // c0 and c1 fail at once; a non-continuation byte ends a character early.
    struct { const char *s; size_t r0, r1; } early[] = {
        { "\xc0\x80", size_t(-1), 0 },
        { "\xc3\x41", size_t(-2), size_t(-1) },
        { "\xe0\xa0\x41", size_t(-2), size_t(-2) },
    };
    for (auto &e : early) {
        mbstate_t bst{};
        wchar_t bwc;
        errno = 0;
        size_t r0 = mbrtowc(&bwc, e.s, 1, &bst);
        bool ok = r0 == e.r0;
        if (r0 == size_t(-2)) {
            size_t r1 = mbrtowc(&bwc, e.s + 1, 1, &bst);
            ok = ok && r1 == e.r1;
            if (r1 == size_t(-2)) {
                errno = 0;
                ok = ok && mbrtowc(&bwc, e.s + 2, 1, &bst) == size_t(-1) && errno == EILSEQ;
            }
        }
        ok = ok && errno == EILSEQ;
        report(ok, std::string("UTF-8 bytewise ") + hex(e.s, strlen(e.s)) + " ends early");
    }
    expect_mbr(tag, "\xc3\x41", 2, size_t(-1), 0, EILSEQ);
    expect_mbr(tag, "\xc3", 1, size_t(-2));
    expect_mbr(tag, "\xc3", 0, size_t(-2));

    // A character split across calls; each call returns the bytes it used.
    mbstate_t st{};
    wchar_t wc = 0;
    size_t r1 = mbrtowc(&wc, "\xe2", 1, &st);
    bool partial = !mbsinit(&st);
    size_t r2 = mbrtowc(&wc, "\x82", 1, &st);
    size_t r3 = mbrtowc(&wc, "\xac", 1, &st);
    report(r1 == size_t(-2) && partial && r2 == size_t(-2) && r3 == 1 && wc == 0x20ac &&
           mbsinit(&st), "UTF-8 mbrtowc split character");
    st = {};
    mbrtowc(&wc, "\xc3", 1, &st);
    errno = 0;
    size_t r = mbrtowc(nullptr, nullptr, 0, &st);
    report(r == size_t(-1) && errno == EILSEQ, "UTF-8 mbrtowc(NULL) mid-character EILSEQ");

    // mbtowc: incomplete input is -1 with errno unchanged. OSv's mbtowc is
    // stateless; glibc keeps the partial character in hidden state after an
    // incomplete sequence, so reset it with mbtowc(NULL, ...) after each
    // failure to keep the test valid on both.
    errno = 0;
    report(mbtowc(&wc, "\xc3", 1) == -1 && errno == 0, "UTF-8 mbtowc incomplete, errno 0");
    mbtowc(nullptr, nullptr, 0);
    errno = 0;
    report(mbtowc(&wc, "\xff", 1) == -1 && errno == EILSEQ, "UTF-8 mbtowc invalid EILSEQ");
    mbtowc(nullptr, nullptr, 0);
    report(mbtowc(&wc, "\xc3\xa9", 2) == 2 && wc == 0xe9, "UTF-8 mbtowc");
    report(mbtowc(nullptr, nullptr, 0) == 0, "UTF-8 mbtowc(NULL) is 0");

    const char *in = "a\xc3\xa9" "b";
    const char *src = in;
    wchar_t out[64];
    st = {};
    report(mbsrtowcs(out, &src, 8, &st) == 3 && !src && out[1] == 0xe9, "UTF-8 mbsrtowcs");
    src = in;
    report(mbsrtowcs(nullptr, &src, 0, &st) == 3 && src == in, "UTF-8 mbsrtowcs count");
    const char *bad = "a\xff" "b";
    src = bad;
    errno = 0;
    report(mbsrtowcs(out, &src, 8, &st) == size_t(-1) && errno == EILSEQ && src == bad + 1,
           "UTF-8 mbsrtowcs stops at the invalid byte");

    report(btowc('a') == L'a' && btowc(0x80) == WEOF && btowc(0xc3) == WEOF && btowc(EOF) == WEOF,
           "UTF-8 btowc");
    report(wctob(L'a') == 'a' && wctob(0x80) == EOF && wctob(0xe9) == EOF, "UTF-8 wctob");

    struct { wchar_t wc; size_t n; const char *bytes; } enc[] = {
        { 0x7f, 1, "\x7f" },
        { 0x80, 2, "\xc2\x80" },
        { 0x7ff, 2, "\xdf\xbf" },
        { 0x800, 3, "\xe0\xa0\x80" },
        { 0xd7ff, 3, "\xed\x9f\xbf" },
        { 0xe000, 3, "\xee\x80\x80" },
        { 0xfffe, 3, "\xef\xbf\xbe" },
        { 0xffff, 3, "\xef\xbf\xbf" },
        { 0x10000, 4, "\xf0\x90\x80\x80" },
        { 0x10ffff, 4, "\xf4\x8f\xbf\xbf" },
        { 0x110000, 4, "\xf4\x90\x80\x80" },
        { 0x1fffff, 4, "\xf7\xbf\xbf\xbf" },
    };
    char buf[8];
    int err;
    for (auto &e : enc) {
        memset(buf, 0, sizeof(buf));
        size_t n = wcr(e.wc, buf, &err);
        report(n == e.n && !memcmp(buf, e.bytes, e.n),
               "UTF-8 wcrtomb " + std::to_string(e.wc) + " = " + std::to_string(long(n)) +
               " " + hex(buf, n <= 8 ? n : 0));
    }
    for (wchar_t s : { wchar_t(0xd800), wchar_t(0xdfff), wchar_t(0x80000000) }) {
        report(wcr(s, buf, &err) == size_t(-1) && err == EILSEQ,
               "UTF-8 wcrtomb " + std::to_string(long(s)) + " EILSEQ");
    }
    // The encoder cap: glibc gives 5 or 6 bytes, OSv EILSEQ.
    struct { wchar_t wc; size_t n; const char *bytes; } big[] = {
        { 0x200000, 5, "\xf8\x88\x80\x80\x80" },
        { 0x7fffffff, 6, "\xfd\xbf\xbf\xbf\xbf\xbf" },
    };
    for (auto &e : big) {
        size_t n = wcr(e.wc, buf, &err);
        report((n == e.n && !memcmp(buf, e.bytes, e.n)) || (n == size_t(-1) && err == EILSEQ),
               "UTF-8 wcrtomb " + std::to_string(e.wc) + " = " + std::to_string(long(n)));
    }
    report(wcrtomb(nullptr, 0xe9, &st) == 1, "UTF-8 wcrtomb(NULL) is 1");

    // wcsrtombs stops before a character that does not fit.
    const wchar_t w4[] = { L'a', 0x10000, L'b', 0 };
    const wchar_t *ws = w4;
    memset(buf, 0, sizeof(buf));
    report(wcsrtombs(buf, &ws, 4, nullptr) == 1 && ws == w4 + 1 && buf[0] == 'a',
           "UTF-8 wcsrtombs stops before 0x10000");
    // 0x7fffffff: glibc stops before it (1); OSv fails with EILSEQ only
    // because of the 4-byte encoder cap described above.
    const wchar_t w6[] = { L'a', 0x7fffffff, L'b', 0 };
    ws = w6;
    errno = 0;
    size_t n6 = wcsrtombs(buf, &ws, 4, nullptr);
    report(n6 == 1 || (n6 == size_t(-1) && errno == EILSEQ),
           "UTF-8 wcsrtombs a,7fffffff,b n=4 = " + std::to_string(long(n6)));

    report(mblen("\xc3\xa9", 2) == 2 && mblen("\xc3", 1) == -1, "UTF-8 mblen");
    mblen(nullptr, 0);
    report(wctomb(buf, 0xe9) == 2, "UTF-8 wctomb");
    report(mbstowcs(out, in, 8) == 3, "UTF-8 mbstowcs");

    report(regex_match("a.b", "a\xc3\xa9" "b"), "UTF-8 regex a.b matches a\\u00e9b");
    report(regex_match("^.$", "\xc3\xa9"), "UTF-8 regex ^.$ matches one character");
    report(fnmatch("?", "\xc3\xa9", 0) == 0, "UTF-8 fnmatch ? one character");
    setlocale(LC_ALL, "C");
}

// mbsnrtowcs never reads past n, even with 5- and 6-byte forms: the input
// ends at a guard page. A partial character at the end is consumed into the
// state, as glibc does.
static void test_mbsnrtowcs_bound()
{
    if (!setlocale(LC_ALL, "C.UTF-8")) {
        return;
    }
    long pg = sysconf(_SC_PAGESIZE);
    char *map = static_cast<char *>(mmap(nullptr, 2 * pg, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (map == MAP_FAILED || mprotect(map + pg, pg, PROT_NONE)) {
        report(false, "mmap guard page");
        return;
    }
    // 30 six-byte characters (enough for musl's n / 4 fast path) and the
    // first five bytes of a sixth-byte form, ending at the guard page.
    const char six[] = "\xfd\xbf\xbf\xbf\xbf\xbf";
    const char part[] = "\xfc\x84\x80\x80\x80";
    size_t n = 30 * 6 + 5;
    char *in = map + pg - n;
    for (int i = 0; i < 30; i++) {
        memcpy(in + 6 * i, six, 6);
    }
    memcpy(in + 180, part, 5);

    wchar_t out[64];
    mbstate_t st{};
    const char *src = in;
    size_t r = mbsnrtowcs(nullptr, &src, n, 0, &st);
    report(r == 30 && src == in && mbsinit(&st), "mbsnrtowcs count = " + std::to_string(long(r)));
    r = mbsnrtowcs(out, &src, n, 64, &st);
    report(r == 30 && src == in + n && out[29] == 0x7fffffff && !mbsinit(&st),
           "mbsnrtowcs = " + std::to_string(long(r)) + " consumed " +
           std::to_string(src ? long(src - in) : -1L));
    wchar_t wc = 0;
    r = mbrtowc(&wc, "\x80", 1, &st);
    report(r == 1 && wc == 0x4000000, "mbsnrtowcs leaves the partial character in the state");

    // A short input ending in a partial form.
    memcpy(map + pg - 7, "ab\xfc\x84\x80\x80\x80", 7);
    src = map + pg - 7;
    st = {};
    r = mbsnrtowcs(out, &src, 7, 64, &st);
    report(r == 2 && src == map + pg && !mbsinit(&st), "mbsnrtowcs short partial");
    munmap(map, 2 * pg);
    setlocale(LC_ALL, "C");
}

// A wide stream oriented in C.UTF-8 writes and reads UTF-8.
static void test_wide_stream()
{
    if (!setlocale(LC_ALL, "C.UTF-8")) {
        return;
    }
    FILE *f = tmpfile();
    if (!f) {
        report(false, "tmpfile");
        setlocale(LC_ALL, "C");
        return;
    }
    bool w = fputwc(0xa2, f) == 0xa2 && fputwc(0x20ac, f) == 0x20ac && fputwc(0x24b62, f) == 0x24b62;
    fflush(f);
    unsigned char b[16] = {};
    ssize_t n = pread(fileno(f), b, sizeof(b), 0);
    report(w && n == 9 && !memcmp(b, "\xc2\xa2\xe2\x82\xac\xf0\xa4\xad\xa2", 9),
           "UTF-8 fputwc writes UTF-8 (" + std::to_string(long(n)) + " bytes)");
    rewind(f);
    wint_t c1 = fgetwc(f), c2 = fgetwc(f), c3 = fgetwc(f);
    report(c1 == 0xa2 && c2 == 0x20ac && c3 == 0x24b62, "UTF-8 fgetwc reads UTF-8");
    fclose(f);
    setlocale(LC_ALL, "C");
}

// MB_CUR_MAX follows LC_CTYPE of the thread's locale.
static void test_locale_objects()
{
    setlocale(LC_ALL, "C");
    setlocale(LC_NUMERIC, "C.UTF-8");
    report(MB_CUR_MAX == 1, "LC_NUMERIC alone does not change MB_CUR_MAX");
    setlocale(LC_CTYPE, "C.UTF-8");
    report(MB_CUR_MAX == 6, "setlocale(LC_CTYPE, C.UTF-8)");
    setlocale(LC_ALL, "C.UTF-8");
    setlocale(LC_CTYPE, "C");
    report(MB_CUR_MAX == 1, "setlocale(LC_CTYPE, C) after C.UTF-8");
    setlocale(LC_ALL, "C");

    locale_t u = newlocale(LC_ALL_MASK, "C.UTF-8", nullptr);
    if (!u) {
        report(false, "newlocale(C.UTF-8)");
        return;
    }
    uselocale(u);
    report(MB_CUR_MAX == 6, "uselocale(C.UTF-8)");
    size_t other = 0;
    std::thread t([&] { other = MB_CUR_MAX; });
    t.join();
    report(other == 1, "another thread keeps the global C");
    wchar_t wc;
    report(mbtowc(&wc, "\xc3\xa9", 2) == 2, "mbtowc follows the thread locale");
    locale_t d = duplocale(u);
    uselocale(LC_GLOBAL_LOCALE);
    report(MB_CUR_MAX == 1, "back on the global locale");
    if (d) {
        uselocale(d);
        report(MB_CUR_MAX == 6, "duplocale copies the UTF-8 ctype");
        uselocale(LC_GLOBAL_LOCALE);
        freelocale(d);
    }
    locale_t c = newlocale(LC_CTYPE_MASK, "C", u);
    if (c) {
        uselocale(c);
        report(MB_CUR_MAX == 1, "newlocale(LC_CTYPE, C, base) clears it");
        uselocale(LC_GLOBAL_LOCALE);
        freelocale(c);
    } else {
        freelocale(u);
    }
}

static const struct wclass {
    const char *name;
    int (*f)(wint_t);
    int (*fl)(wint_t, locale_t);
} wclasses[12] = {
    {"alnum", iswalnum, iswalnum_l}, {"alpha", iswalpha, iswalpha_l},
    {"blank", iswblank, iswblank_l}, {"cntrl", iswcntrl, iswcntrl_l},
    {"digit", iswdigit, iswdigit_l}, {"graph", iswgraph, iswgraph_l},
    {"lower", iswlower, iswlower_l}, {"print", iswprint, iswprint_l},
    {"punct", iswpunct, iswpunct_l}, {"space", iswspace, iswspace_l},
    {"upper", iswupper, iswupper_l}, {"xdigit", iswxdigit, iswxdigit_l},
};

// Mismatches of one property over the sample, reported once.
struct tally {
    int bad = 0;
    unsigned first = 0;
    void add(bool ok, unsigned cp)
    {
        if (!ok && !bad++) {
            first = cp;
        }
    }
    void report_as(const std::string &what) const
    {
        char b[64];
        snprintf(b, sizeof(b), ": %d mismatches, first at %#x", bad, first);
        report(!bad, what + " over " + std::to_string(sizeof(ctype_sample) / sizeof(*ctype_sample)) +
               " code points" + (bad ? b : ""));
    }
};

// The sample in locale name: the plain functions and descriptors with name
// current, then the _l functions with name's object while the other locale
// is current (iswctype_l and towctrans_l get the other object, ignored).
static void test_wide_sample(const char *name, bool utf8)
{
    const char *other = utf8 ? "C" : "C.UTF-8";
    locale_t l = newlocale(LC_ALL_MASK, name, nullptr);
    locale_t lo = newlocale(LC_ALL_MASK, other, nullptr);
    if (!l || !lo || !setlocale(LC_ALL, name)) {
        report(false, std::string("locale ") + name);
        return;
    }
    tally cls[12], cls_l[12], up, up_l, low, low_l, width, ws;
    wctype_t t[12], t_l[12];
    for (int i = 0; i < 12; i++) {
        t[i] = wctype(wclasses[i].name);
        t_l[i] = wctype_l(wclasses[i].name, l);
    }
    wctrans_t tu = wctrans("toupper"), tl = wctrans("tolower");
    wctrans_t tu_l = wctrans_l("toupper", l), tl_l = wctrans_l("tolower", l);
    for (int pass = 0; pass < 2; pass++) {
        setlocale(LC_ALL, pass ? other : name);
        for (auto &r : ctype_sample) {
            wint_t wc = r.cp;
            unsigned bits = utf8 ? r.ubits : r.cbits;
            wint_t eu = wc + (utf8 ? r.uup : r.cup), el = wc + (utf8 ? r.ulo : r.clo);
            int w = int(utf8 ? r.w >> 2 : r.w & 3) - 1;
            for (int i = 0; i < 12; i++) {
                bool e = bits >> i & 1;
                if (pass) {
                    cls_l[i].add(!!wclasses[i].fl(wc, l) == e && !!iswctype_l(wc, t_l[i], lo) == e, r.cp);
                } else {
                    cls[i].add(!!wclasses[i].f(wc) == e && !!iswctype(wc, t[i]) == e, r.cp);
                }
            }
            if (pass) {
                up_l.add(towupper_l(wc, l) == eu && towctrans_l(wc, tu_l, lo) == eu, r.cp);
                low_l.add(towlower_l(wc, l) == el && towctrans_l(wc, tl_l, lo) == el, r.cp);
            } else {
                up.add(towupper(wc) == eu && towctrans(wc, tu) == eu, r.cp);
                low.add(towlower(wc) == el && towctrans(wc, tl) == el, r.cp);
                width.add(wcwidth(wchar_t(wc)) == w, r.cp);
                wchar_t s2[2] = {wchar_t(wc), L'a'};
                ws.add(wcswidth(s2, 2) == (w < 0 || !wc ? (wc ? -1 : 0) : w + 1), r.cp);
            }
        }
    }
    std::string n = name;
    for (int i = 0; i < 12; i++) {
        cls[i].report_as(n + " isw" + wclasses[i].name + "/iswctype");
        cls_l[i].report_as(n + " isw" + wclasses[i].name + "_l/iswctype_l");
    }
    up.report_as(n + " towupper/towctrans");
    up_l.report_as(n + " towupper_l/towctrans_l");
    low.report_as(n + " towlower/towctrans");
    low_l.report_as(n + " towlower_l/towctrans_l");
    width.report_as(n + " wcwidth");
    ws.report_as(n + " wcswidth");
    freelocale(l);
    freelocale(lo);
    setlocale(LC_ALL, "C");
}

// Descriptor binding and invalid names.
static void test_wide_descriptors()
{
    setlocale(LC_ALL, "C");
    locale_t c = newlocale(LC_ALL_MASK, "C", nullptr);
    locale_t u = newlocale(LC_ALL_MASK, "C.UTF-8", nullptr);
    if (!c || !u) {
        report(false, "newlocale C / C.UTF-8");
        return;
    }
    wctype_t ac = wctype("alpha");
    wctrans_t uc = wctrans("toupper");
    report(!iswalpha(0xe9) && towupper(0xe9) == 0xe9 && wcwidth(0xe9) == -1, "C: 0xe9 no class, identity, width -1");
    report(iswalpha_l(0xe9, u) && towupper_l(0xe9, u) == 0xc9 && !iswalpha_l(0xe9, c),
           "C: isw*_l and tow*_l follow their locale");
    setlocale(LC_ALL, "C.UTF-8");
    wctype_t au = wctype("alpha");
    wctrans_t uu = wctrans("toupper");
    report(iswalpha(0xe9) && towupper(0xe9) == 0xc9 && wcwidth(0xe9) == 1, "C.UTF-8: 0xe9 alpha, upper 0xc9, width 1");
    report(!iswctype(0xe9, ac) && towctrans(0xe9, uc) == 0xe9, "C descriptors stay C under C.UTF-8");
    report(iswctype(L'a', ac) && towctrans(L'a', uc) == L'A', "C descriptors keep ASCII under C.UTF-8");
    setlocale(LC_ALL, "C");
    report(iswctype(0xe9, au) && towctrans(0xe9, uu) == 0xc9, "C.UTF-8 descriptors stay UTF-8 under C");
    report(!iswctype_l(0xe9, ac, u) && towctrans_l(0xe9, uc, u) == 0xe9, "iswctype_l/towctrans_l ignore a UTF-8 locale");
    report(iswctype_l(0xe9, au, c) && towctrans_l(0xe9, uu, c) == 0xc9, "iswctype_l/towctrans_l ignore a C locale");
    report(iswctype(0xe9, wctype_l("alpha", u)) && towctrans(0xe9, wctrans_l("toupper", u)) == 0xc9,
           "wctype_l/wctrans_l bind their UTF-8 locale");
    setlocale(LC_ALL, "C.UTF-8");
    report(!iswctype(0xe9, wctype_l("alpha", c)) && towctrans(0xe9, wctrans_l("toupper", c)) == 0xe9,
           "wctype_l/wctrans_l bind their C locale");
    setlocale(LC_ALL, "C");
    uselocale(u);
    wctype_t at = wctype("alpha");
    uselocale(LC_GLOBAL_LOCALE);
    report(iswctype(0xe9, at), "wctype under uselocale(C.UTF-8) binds UTF-8");
    report(!wctype("bogus") && !wctype("") && !wctype("Alpha") && !wctype_l("bogus", u),
           "wctype of an invalid name is 0");
    report(!wctrans("bogus") && !wctrans("") && !wctrans("TOUPPER") && !wctrans_l("bogus", u),
           "wctrans of an invalid name is NULL");
    report(!iswctype(L'a', 0) && towctrans(L'a', nullptr) == L'a', "null descriptors: no class, identity");
    report(wcswidth(L"ab\0c", 4) == 2 && wcswidth(L"abc", 2) == 2 && wcswidth(L"", 5) == 0,
           "wcswidth stops at n and at L'\\0'");
    setlocale(LC_ALL, "C.UTF-8");
    report(wcswidth(L"e\u0301\u4e00", 3) == 3 && wcswidth(L"a\x7f", 2) == -1, "C.UTF-8 wcswidth");
    setlocale(LC_ALL, "C");
    report(wcswidth(L"a\u00e9", 2) == -1, "C wcswidth of non-ASCII is -1");
    freelocale(c);
    freelocale(u);
}

static FILE *tmp()
{
    FILE *f = tmpfile();
    if (!f) {
        perror("tmpfile");
        exit(1);
    }
    return f;
}

// A fresh stream (no orientation) reading the bytes s[0..n).
static FILE *wio_in(const char *s, size_t n)
{
    FILE *f = tmp();
    if (write(fileno(f), s, n) != ssize_t(n) || lseek(fileno(f), 0, SEEK_SET) != 0) {
        perror("wio_in");
        exit(1);
    }
    return f;
}

// What the stream wrote, as hex; closes it.
static std::string wio_out(FILE *f)
{
    fflush(f);
    char b[64];
    ssize_t n = pread(fileno(f), b, sizeof(b), 0);
    fclose(f);
    return n < 0 ? "error" : hex(b, n);
}

static void test_wio(const char *loc, bool u)
{
    if (!setlocale(LC_ALL, loc)) {
        report(false, std::string("setlocale ") + loc);
        return;
    }
    std::string L = std::string(loc) + " ";
    FILE *f;
    wint_t q, q2, q3, q4;
    int r, e, fe;

    // Byte-backed wide output.
    struct { wchar_t in[4]; const char *c, *utf8; } outs[] = {
        { L"\u00e9", "3f", "c3a9" }, { L"\u4e00", "3f", "e4b880" }, { L"A\u00e9" "B", "413f42", "41c3a942" },
    };
    for (auto &o : outs) {
        f = tmp();
        bool ok = true;
        errno = 0;
        for (const wchar_t *p = o.in; ok && *p; p++) {
            ok = fputwc(*p, f) == wint_t(*p);
        }
        ok = ok && errno == 0 && !ferror(f);
        std::string got = wio_out(f);
        report(ok && got == (u ? o.utf8 : o.c), L + "fputwc of " + std::to_string(wcslen(o.in)) +
               " -> " + got);
    }
    f = tmp(); errno = 0; r = fputws(L"a\u00e9z", f); e = errno; fe = ferror(f);
    report(r == 1 && !e && !fe && wio_out(f) == (u ? "61c3a97a" : "613f7a"), L + "fputws(a e9 z)");
    f = tmp(); errno = 0; r = fwprintf(f, L"%lc", (wint_t)0xe9); e = errno;
    report(r == 1 && !e && wio_out(f) == (u ? "c3a9" : "3f"), L + "fwprintf(%lc e9)");
    f = tmp(); errno = 0; r = fwprintf(f, L"%s", "\xc3\xa9"); e = errno; fe = ferror(f);
    report((u ? r == 1 && !e : r == -1 && e == EILSEQ) && !fe && wio_out(f) == (u ? "c3a9" : ""),
           L + "fwprintf(%s c3a9)");
    f = tmp(); errno = 0; r = fwprintf(f, L"<%c>", 0xe9); e = errno;
    report(r == 3 && !e && wio_out(f) == "3c3f3e", L + "fwprintf(<%c e9>)");
    f = tmp(); report(fputws(L"", f) == 1, L + "fputws(\"\")"); fclose(f);
    f = tmp(); fputc('x', f); q = putwc('y', f);
    report(q == 'y' && wio_out(f) == "7879", L + "byte-oriented putwc(y)");

    // Byte-backed wide input.
    if ((f = wio_in("\xc3\xa9z", 3))) {
        errno = 0; q = fgetwc(f); e = errno; fe = ferror(f); q2 = fgetwc(f);
        report(u ? q == 0xe9 && !e && !fe && q2 == 'z' : q == WEOF && e == EILSEQ && fe && q2 == WEOF,
               L + "fgetwc(c3 a9 z)");
        fclose(f);
    }
    if ((f = wio_in("\xc3\xa9z", 3))) {
        q = fgetwc(f); clearerr(f); q2 = fgetwc(f); q3 = fgetwc(f);
        report(u ? q == 0xe9 && q2 == 'z' && q3 == WEOF && ftell(f) == 3
                 : q == WEOF && q2 == WEOF && q3 == WEOF && ftell(f) == 0, L + "fgetwc(c3a9z) clearerr ftell");
        fclose(f);
    }
    if ((f = wio_in("\xe9z", 2))) {
        errno = 0; q = fgetwc(f); e = errno; fe = ferror(f); q2 = fgetwc(f);
        report(q == WEOF && e == EILSEQ && fe && q2 == WEOF, L + "fgetwc(e9 z)");
        fclose(f);
    }
    if ((f = wio_in("\xc3", 1))) {
        errno = 0; q = fgetwc(f); e = errno; fe = ferror(f); int eo = feof(f);
        clearerr(f); q2 = fgetwc(f);
        // C pushes 0xc3 back; C.UTF-8 consumes it, so the next read is at EOF.
        report(q == WEOF && e == EILSEQ && fe && !eo && q2 == WEOF && (feof(f) != 0) == u,
               L + "fgetwc(c3 EOF)");
        fclose(f);
    }
    if ((f = wio_in("a\xe9z", 3))) {
        q = fgetwc(f); errno = 0; q2 = fgetwc(f); e = errno; fe = ferror(f);
        clearerr(f); errno = 0; q3 = fgetwc(f);
        report(q == 'a' && q2 == WEOF && e == EILSEQ && fe && q3 == WEOF && errno == EILSEQ && ftell(f) == 1,
               L + "fgetwc(a e9 z) clearerr ftell");
        fclose(f);
    }
    if ((f = wio_in("z", 1))) {
        errno = 0; q = ungetwc(0xe9, f); e = errno; q2 = fgetwc(f); q3 = fgetwc(f);
        report(q == 0xe9 && !e && q2 == 0xe9 && q3 == 'z', L + "ungetwc(e9)");
        fclose(f);
    }
    if ((f = wio_in("z", 1))) {
        errno = 0; q = ungetwc(0x4e00, f); e = errno; q2 = fgetwc(f);
        report(q == 0x4e00 && !e && q2 == 0x4e00, L + "ungetwc(4e00)");
        fclose(f);
    }
    if ((f = wio_in("ab", 2))) {
        errno = 0; q = ungetwc(0xe9, f); q2 = ungetwc(0x4e00, f); q3 = ungetwc('q', f); q4 = ungetwc(0x20ac, f);
        bool ok = q == 0xe9 && q2 == 0x4e00 && q3 == 'q' && q4 == 0x20ac && !errno;
        const wint_t want[] = { 0x20ac, 'q', 0x4e00, 0xe9, 'a', 'b' };
        for (wint_t w : want) {
            ok = ok && fgetwc(f) == w;
        }
        report(ok, L + "4x ungetwc");
        fclose(f);
    }
    if ((f = wio_in("abc", 3))) {
        fgetwc(f); ungetwc(0xe9, f); fseek(f, 0, SEEK_SET);
        report(fgetwc(f) == 'a', L + "ungetwc(e9) then fseek(0)");
        fclose(f);
    }
    if ((f = wio_in("abc", 3))) {
        fgetwc(f); ungetwc(0xe9, f);
        report(ungetwc(WEOF, f) == WEOF, L + "ungetwc(WEOF)");
        fclose(f);
    }
    if ((f = wio_in("ab", 2))) {
        fgetc(f); errno = 0; q = ungetwc('x', f); e = errno;
        report(q == 'x' && !e && fgetc(f) == 'x', L + "byte-oriented ungetwc");
        fclose(f);
    }
    if ((f = wio_in("ab", 2))) {
        int before = fwide(f, 0);
        fgetwc(f);
        report(before == 0 && fwide(f, 0) == 1, L + "fwide after fgetwc");
        fclose(f);
    }
    if ((f = wio_in("a\xc3\xa9\n", 4))) {
        wchar_t wb[8];
        errno = 0; wchar_t *p = fgetws(wb, 8, f); e = errno;
        // errno on success is not checked: musl's fgetws leaves EAGAIN.
        report(u ? p && wb[0] == 'a' && wb[1] == 0xe9 : !p && e == EILSEQ, L + "fgetws(a c3a9)");
        fclose(f);
    }
    if ((f = wio_in("\xc3\xa9 ", 3))) {
        wchar_t ws[8] = {};
        errno = 0; r = fwscanf(f, L"%ls", ws); e = errno;
        report(u ? r == 1 && !e && ws[0] == 0xe9 : r == -1 && e == EILSEQ && !ws[0], L + "fwscanf(%ls c3a9)");
        fclose(f);
    }

    // Wide-buffer streams never convert.
    wchar_t ws[8] = {};
    errno = 0; r = swprintf(ws, 8, L"%lc%lc", (wint_t)0xd800, (wint_t)0x7fffffff);
    report(r == 2 && !errno && ws[0] == 0xd800 && ws[1] == 0x7fffffff, L + "swprintf(d800 7fffffff)");
    r = swprintf(ws, 8, L"%ls", L"\uff41\u00e9");
    report(r == 2 && ws[0] == 0xff41 && ws[1] == 0xe9, L + "swprintf(%ls ff41 e9)");
    errno = 0; r = swprintf(ws, 8, L"%s", "\xc3\xa9");
    report(u ? r == 1 && ws[0] == 0xe9 : r == -1 && errno == EILSEQ, L + "swprintf(%s c3a9)");
    wchar_t *mb = nullptr;
    size_t ms = 0;
    if ((f = open_wmemstream(&mb, &ms))) {
        fputwc(0xd800, f); fputwc(wchar_t(0x80000000u), f); fputwc(WEOF, f); fclose(f);
        report(ms == 3 && mb[0] == 0xd800 && wint_t(mb[1]) == 0x80000000u && wint_t(mb[2]) == WEOF,
               L + "wmemstream(d800 80000000 WEOF)");
        free(mb);
    }
    wchar_t wb[4] = {};
    r = swscanf(L"\xd800\x4e00 ", L"%ls", wb);
    report(r == 1 && wb[0] == 0xd800 && wb[1] == 0x4e00, L + "swscanf(%ls d800 4e00)");
    // The allocating forms, past their first 31-element buffer.
    std::wstring big(40, wchar_t(0x4e00));
    big += L" ab";
    wchar_t *mp = nullptr;
    char *cp = nullptr;
    r = swscanf(big.c_str(), L"%mls %ms", &mp, &cp);
    report(r == 2 && mp && wcslen(mp) == 40 && mp[39] == 0x4e00 && cp && !strcmp(cp, "ab"),
           L + "swscanf(%mls %ms)");
    free(mp);
    free(cp);
    setlocale(LC_ALL, "C");
}

// A byte-backed wide stream's conversion is bound at its first wide operation.
static void test_wio_bind()
{
    locale_t u = newlocale(LC_ALL_MASK, "C.UTF-8", nullptr);
    if (!u) {
        report(false, "newlocale C.UTF-8");
        return;
    }
    setlocale(LC_ALL, "C.UTF-8");
    FILE *f = tmp();
    fwide(f, 1);
    setlocale(LC_ALL, "C");
    errno = 0;
    wint_t q = fputwc(0xe9, f);
    report(q == 0xe9 && !errno && wio_out(f) == "c3a9", "bind: oriented in C.UTF-8, written in C");
    f = tmp();
    setlocale(LC_ALL, "C.UTF-8");
    q = fputwc(0xe9, f);
    report(q == 0xe9 && wio_out(f) == "c3a9", "bind: opened in C, first wide op in C.UTF-8");
    setlocale(LC_ALL, "C");
    uselocale(u);
    f = tmp();
    q = fputwc(0xe9, f);
    uselocale(LC_GLOBAL_LOCALE);
    report(q == 0xe9 && wio_out(f) == "c3a9", "bind: uselocale(C.UTF-8) thread, global C");
    freelocale(u);
}

// iconv converts UTF-8 as Unicode in any locale: forms above U+10FFFF (which
// mbrtowc accepts in C.UTF-8, as glibc's does) are EILSEQ, and the output
// pointer never moves backwards. glibc rejects them too when converting to
// UTF-16 or UTF-32, but passes them through to UTF-8; OSv rejects them there
// as well (rows with copy_ok accept either result, libc/locale/README.md).
// A sequence cut off at the end of the input is EINVAL, with nothing of it
// consumed.
static void test_iconv(const char *loc)
{
    setlocale(LC_ALL, loc);
    std::string L = std::string(loc) + " iconv ";
    struct row { const char *to, *in; size_t n, consumed, produced; bool ok, copy_ok; const char *what; int err = EILSEQ; };
    const row rows[] = {
        { "UTF-8", "a\xf8\x88\x80\x80\x80" "b", 7, 1, 1, false, true, "5-byte U+200000 to UTF-8" },
        { "UTF-8", "a\xf4\x90\x80\x80" "b", 6, 1, 1, false, true, "U+110000 to UTF-8" },
        { "UTF-8", "a\xfd\xbf\xbf\xbf\xbf\xbf" "b", 8, 1, 1, false, true, "6-byte U+7FFFFFFF to UTF-8" },
        { "UTF-16LE", "a\xf4\x90\x80\x80" "b", 6, 1, 2, false, false, "U+110000 to UTF-16LE" },
        { "UTF-8", "a\xf4\x8f\xbf\xbf" "b", 6, 6, 6, true, false, "U+10FFFF to UTF-8" },
        { "UTF-16LE", "a\xf4\x8f\xbf\xbf" "b", 6, 6, 8, true, false, "U+10FFFF to UTF-16LE" },
        { "UTF-16LE", "a\xf4\x8f\xbf", 4, 1, 2, false, false, "truncated U+10FFFF to UTF-16LE", EINVAL },
    };
    for (auto &r : rows) {
        iconv_t cd = iconv_open(r.to, "UTF-8");
        if (cd == (iconv_t)-1) {
            report(false, L + r.what + ": iconv_open");
            continue;
        }
        char guard_lo[16], out[64], guard_hi[16];
        memset(guard_lo, 0x5a, sizeof guard_lo);
        memset(guard_hi, 0x5a, sizeof guard_hi);
        char *ip = const_cast<char *>(r.in), *op = out;
        size_t il = r.n, ol = sizeof out;
        errno = 0;
        size_t ret = iconv(cd, &ip, &il, &op, &ol);
        int e = errno;
        bool guards = true;
        for (size_t i = 0; i < sizeof guard_lo; i++) {
            guards = guards && guard_lo[i] == 0x5a && guard_hi[i] == 0x5a;
        }
        report(op >= out && op <= out + sizeof out && ol <= sizeof out && guards,
               L + r.what + ": output stays in the buffer");
        if (r.copy_ok && ret == 0) {
            // glibc: copied unchanged.
            report(r.n - il == r.n && sizeof out - ol == r.n && !memcmp(out, r.in, r.n),
                   L + r.what + ": copied unchanged");
            iconv_close(cd);
            continue;
        }
        report(r.ok ? ret == 0 : (ret == size_t(-1) && e == r.err),
               L + r.what + ": " + (r.ok ? "converts" : r.err == EINVAL ? "EINVAL" : "EILSEQ"));
        report(r.n - il == r.consumed && sizeof out - ol == r.produced,
               L + r.what + ": consumed " + std::to_string(r.n - il) + " produced " +
               std::to_string(sizeof out - ol));
        iconv_close(cd);
    }
    setlocale(LC_ALL, "C");
}

int main()
{
    test_c();
    test_utf8();
    test_mbsnrtowcs_bound();
    test_wide_stream();
    test_locale_objects();
    test_wide_sample("C", false);
    test_wide_sample("C.UTF-8", true);
    test_wide_descriptors();
    test_wio("C", false);
    test_wio("C.UTF-8", true);
    test_wio_bind();
    test_iconv("C");
    test_iconv("C.UTF-8");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
