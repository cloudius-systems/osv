/*
 * Copyright © 2005-2020 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Modified for OSv from musl 1.2.1 src/stdio/fputwc.c: wide-buffer streams
 * (F_WBUF) store the wchar_t unconverted; on a byte-backed stream an
 * unencodable character (WEOF included, which musl's vfwprintf passes for
 * a %c it cannot convert) is written as its C-locale transliteration or '?'
 * and the call succeeds, as glibc does. glibc's fputwc(WEOF) as the first
 * output on a stream writes nothing (libc/locale/README.md).
 */
#include "stdio_impl.h"
#include "locale_impl.h"
#include <wchar.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>

hidden _Bool __osv_c_translit(wchar_t, const char **, size_t *);

wint_t __fputwc_unlocked(wchar_t c, FILE *f)
{
	char mbc[MB_LEN_MAX];
	const char *s = mbc;
	size_t l;
	int e = errno;
	locale_t *ploc = &CURRENT_LOCALE, loc = *ploc;

	if (f->mode <= 0) fwide(f, 1);
	if (f->flags & F_WBUF) return f->wput(c, f);
	*ploc = f->locale;

	if (isascii(c)) {
		if ((c = putc_unlocked(c, f)) == WEOF) f->flags |= F_ERR;
	} else {
		l = wctomb(mbc, c);
		if (l == (size_t)-1) {
			errno = e;
			if (OSV_CUR_UTF8 || !__osv_c_translit(c, &s, &l))
				s = "?", l = 1;
		}
		if (__fwritex((void *)s, l, f) < l) c = WEOF, f->flags |= F_ERR;
	}
	*ploc = loc;
	return c;
}

wint_t fputwc(wchar_t c, FILE *f)
{
	FLOCK(f);
	c = __fputwc_unlocked(c, f);
	FUNLOCK(f);
	return c;
}

weak_alias(__fputwc_unlocked, fputwc_unlocked);
weak_alias(__fputwc_unlocked, putwc_unlocked);
