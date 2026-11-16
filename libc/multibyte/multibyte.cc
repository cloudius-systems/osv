/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// mbrtowc(), mbtowc(), mbsrtowcs(), mbsnrtowcs(), btowc(), wcrtomb() and
// wctob() with glibc's behaviour in OSv's two locales. musl's wcsrtombs,
// wcsnrtombs, wcstombs, mbstowcs, mblen, mbrlen and wctomb are built on these.
//
// C: bytes 0..0x7f are characters; any other byte is EILSEQ, and so is any
// wide character >= 0x80.
//
// C.UTF-8: glibc's UTF-8 decoder, which accepts the legacy 5- and 6-byte
// forms (up to 0x7fffffff) and code points above 0x10ffff. Like glibc's, it
// rejects an overlong form or a surrogate only at the byte that completes it
// (e0 80 80 is -2, -2, then -1 byte by byte); a byte that is not a
// continuation is rejected where it appears. The
// encoder stops at 0x1fffff (4 bytes): glibc would emit 5 or 6 bytes there,
// but musl's stdio has 4-byte buffers (MB_LEN_MAX), and such values are never
// Unicode. MB_CUR_MAX still reports glibc's 6.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <osv/export.h>

#include "libc/internal/locale_impl.h"

namespace {

// A partial UTF-8 character in the first word of an mbstate_t (musl's
// mbsinit() reads that word): bits 0..25 hold the value so far, 26..28 the
// bytes still needed, 29..31 the total length. Zero is the initial state.
constexpr unsigned rem_shift = 26, len_shift = 29, val_mask = (1u << rem_shift) - 1;

unsigned &state_word(mbstate_t *st)
{
    return *reinterpret_cast<unsigned *>(st);
}

size_t ilseq(mbstate_t *st)
{
    state_word(st) = 0;
    errno = EILSEQ;
    return size_t(-1);
}

size_t c_mbrtowc(wchar_t *wc, const unsigned char *s, size_t n, mbstate_t *st)
{
    state_word(st) = 0;
    if (!n) {
        return size_t(-2);
    }
    if (*s >= 0x80) {
        return ilseq(st);
    }
    if (wc) {
        *wc = *s;
    }
    return *s != 0;
}

// The smallest value of each length; anything less is an overlong form.
constexpr unsigned min_value[7] = { 0, 0, 0x80, 0x800, 0x10000, 0x200000, 0x4000000 };

size_t utf8_mbrtowc(wchar_t *wc, const unsigned char *s, size_t n, mbstate_t *st)
{
    unsigned c = state_word(st);
    if (!n) {
        return size_t(-2);
    }
    unsigned val, rem, len;
    size_t i = 0;
    if (!c) {
        unsigned b = s[0];
        if (b < 0x80) {
            if (wc) {
                *wc = b;
            }
            return b != 0;
        }
        len = b < 0xc2 ? 0 : b < 0xe0 ? 2 : b < 0xf0 ? 3 : b < 0xf8 ? 4 :
            b < 0xfc ? 5 : b < 0xfe ? 6 : 0;
        if (!len) {
            return ilseq(st);
        }
        val = b & (0x7f >> len);
        rem = len - 1;
        i = 1;
    } else {
        val = c & val_mask;
        rem = (c >> rem_shift) & 7;
        len = c >> len_shift;
    }
    for (; i < n; i++) {
        unsigned b = s[i];
        if ((b & 0xc0) != 0x80) {
            return ilseq(st);
        }
        val = val << 6 | (b & 0x3f);
        rem--;
        if (!rem) {
            if (val < min_value[len] || (val >= 0xd800 && val <= 0xdfff)) {
                return ilseq(st);
            }
            state_word(st) = 0;
            if (wc) {
                *wc = val;
            }
            return i + 1;
        }
    }
    state_word(st) = val | rem << rem_shift | len << len_shift;
    return size_t(-2);
}

size_t do_mbrtowc(wchar_t *wc, const char *src, size_t n, mbstate_t *st)
{
    auto s = reinterpret_cast<const unsigned char *>(src);
    return OSV_CUR_UTF8 ? utf8_mbrtowc(wc, s, n, st) : c_mbrtowc(wc, s, n, st);
}

} // namespace

extern "C" OSV_LIBC_API
size_t mbrtowc(wchar_t *__restrict wc, const char *__restrict s, size_t n,
               mbstate_t *__restrict st) noexcept
{
    static mbstate_t internal;
    if (!st) {
        st = &internal;
    }
    if (!s) {
        // mbrtowc(NULL, "", 1, st): the initial state, or EILSEQ mid-character.
        if (state_word(st) && OSV_CUR_UTF8) {
            return ilseq(st);
        }
        state_word(st) = 0;
        return 0;
    }
    return do_mbrtowc(wc, s, n, st);
}

// Stateless (C and UTF-8 have no shift states): incomplete input is -1 with
// errno unchanged, invalid input -1 with EILSEQ.
extern "C" OSV_LIBC_API
int mbtowc(wchar_t *__restrict wc, const char *__restrict s, size_t n) noexcept
{
    if (!s) {
        return 0;
    }
    mbstate_t st{};
    size_t r = do_mbrtowc(wc, s, n, &st);
    return r >= size_t(-2) ? -1 : int(r);
}

extern "C" OSV_LIBC_API
size_t mbsrtowcs(wchar_t *__restrict dst, const char **__restrict src, size_t len,
                 mbstate_t *__restrict st) noexcept
{
    static mbstate_t internal;
    if (!st) {
        st = &internal;
    }
    // Counting only leaves *src and the state alone, as glibc does.
    mbstate_t tmp = *st;
    mbstate_t *ps = dst ? st : &tmp;
    const char *s = *src;
    size_t cnt = 0;
    while (!dst || cnt < len) {
        wchar_t wc;
        size_t r = do_mbrtowc(&wc, s, size_t(-1), ps);
        if (r == size_t(-1)) {
            if (dst) {
                *src = s;
            }
            return r;
        }
        if (r == 0) {
            if (dst) {
                dst[cnt] = 0;
                *src = nullptr;
            }
            return cnt;
        }
        if (dst) {
            dst[cnt] = wc;
        }
        s += r;
        cnt++;
    }
    *src = s;
    return cnt;
}

// musl's mbsnrtowcs converts n / 4 characters at a time with mbsrtowcs,
// assuming at most 4 bytes each; the 5- and 6-byte forms would make it read
// past n. This one never reads past n. As in glibc, a partial character at the
// end is consumed into the state and *src advances past it; counting only
// (dst NULL) leaves *src and the state alone.
extern "C" OSV_LIBC_API
size_t mbsnrtowcs(wchar_t *__restrict dst, const char **__restrict src, size_t n,
                  size_t wn, mbstate_t *__restrict st) noexcept
{
    static mbstate_t internal;
    if (!st) {
        st = &internal;
    }
    mbstate_t tmp = *st;
    mbstate_t *ps = dst ? st : &tmp;
    const char *s = *src;
    size_t cnt = 0;
    while (n && (!dst || cnt < wn)) {
        wchar_t wc;
        size_t r = do_mbrtowc(&wc, s, n, ps);
        if (r == size_t(-1)) {
            if (dst) {
                *src = s;
            }
            return r;
        }
        if (r == size_t(-2)) {
            s += n;
            break;
        }
        if (r == 0) {
            if (dst) {
                dst[cnt] = 0;
                *src = nullptr;
            }
            return cnt;
        }
        if (dst) {
            dst[cnt] = wc;
        }
        s += r;
        n -= r;
        cnt++;
    }
    if (dst) {
        *src = s;
    }
    return cnt;
}

extern "C" OSV_LIBC_API
wint_t btowc(int c) noexcept
{
    return unsigned(c) < 0x80 ? c : WEOF;
}

extern "C" OSV_LIBC_API
int wctob(wint_t wc) noexcept
{
    return unsigned(wc) < 0x80 ? int(wc) : EOF;
}

extern "C" OSV_LIBC_API
size_t wcrtomb(char *__restrict s, wchar_t wc, mbstate_t *__restrict st) noexcept
{
    if (!s) {
        if (st) {
            state_word(st) = 0;
        }
        return 1;
    }
    unsigned w = wc;
    if (w < 0x80) {
        *s = char(w);
        return 1;
    }
    if (!OSV_CUR_UTF8 || w > 0x1fffff || (w >= 0xd800 && w <= 0xdfff)) {
        errno = EILSEQ;
        return size_t(-1);
    }
    if (w < 0x800) {
        s[0] = char(0xc0 | w >> 6);
        s[1] = char(0x80 | (w & 0x3f));
        return 2;
    }
    if (w < 0x10000) {
        s[0] = char(0xe0 | w >> 12);
        s[1] = char(0x80 | ((w >> 6) & 0x3f));
        s[2] = char(0x80 | (w & 0x3f));
        return 3;
    }
    s[0] = char(0xf0 | w >> 18);
    s[1] = char(0x80 | ((w >> 12) & 0x3f));
    s[2] = char(0x80 | ((w >> 6) & 0x3f));
    s[3] = char(0x80 | (w & 0x3f));
    return 4;
}

// musl's iconv decodes UTF-8 through this (-Dmbrtowc=...): Unicode only. The
// locale decoder also accepts glibc's 0x110000..0x7fffffff forms, which
// iconv's UTF-8 encoder (wctomb, capped at 4 bytes) cannot write back and
// which glibc's iconv also rejects when converting to UTF-16 or UTF-32.
extern "C" OSV_HIDDEN
size_t __osv_mbrtowc_unicode(wchar_t *__restrict wc, const char *__restrict s,
                             size_t n, mbstate_t *__restrict st)
{
    wchar_t c;
    size_t r = utf8_mbrtowc(&c, reinterpret_cast<const unsigned char *>(s), n, st);
    if (r < size_t(-2) && unsigned(c) > 0x10ffff) {
        return ilseq(st);
    }
    if (r < size_t(-2) && wc) {
        *wc = c;
    }
    return r;
}

// musl's regex and fnmatch call mbtowc through this (-Dmbtowc=...): in C each
// high byte is one character, musl's code unit 0xdf80 + (b & 0x7f), as glibc's
// C regex and fnmatch treat it.
extern "C" OSV_HIDDEN
int __osv_mbtowc_cu(wchar_t *__restrict wc, const char *__restrict s, size_t n)
{
    if (s && n && !OSV_CUR_UTF8 && (unsigned char)*s >= 0x80) {
        if (wc) {
            *wc = 0xdf80 + (*s & 0x7f);
        }
        return 1;
    }
    return mbtowc(wc, s, n);
}
