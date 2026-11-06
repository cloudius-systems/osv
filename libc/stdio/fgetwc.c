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
 * Modified for OSv from musl 1.2.1 src/stdio/fgetwc.c: wide-buffer streams
 * (F_WBUF) read through f->wget unconverted; ungetwc() pushback of a
 * character the stream locale cannot encode (see ungetwc.c) is returned
 * as is; on an invalid sequence every byte read is pushed back and the
 * stream gets an error, and a sequence truncated by end of file is consumed
 * with an error but not end of file, as glibc does.
 */
#include "stdio_impl.h"
#include "locale_impl.h"
#include <wchar.h>
#include <errno.h>
#include <string.h>

static wint_t __fgetwc_unlocked_internal(FILE *f)
{
	wchar_t wc;
	int c, n = 0;
	size_t l;
	unsigned char b[8];
	mbstate_t st = { 0 };

	/* Pushback of a character the stream locale cannot encode */
	if (f->rpos >= f->wpb_hi || !f->rend) {
		f->wpb_lo = f->wpb_hi = 0;
	} else if (f->rpos >= f->wpb_lo && *f->rpos == 0xff &&
	           f->rpos + 1 + sizeof wc <= f->wpb_hi) {
		memcpy(&wc, f->rpos + 1, sizeof wc);
		f->rpos += 1 + sizeof wc;
		return wc;
	}

	/* Convert character from buffer if possible */
	if (f->rpos != f->rend) {
		l = mbtowc(&wc, (void *)f->rpos, f->rend - f->rpos);
		if (l+1 >= 1) {
			f->rpos += l + !l; /* l==0 means 1 byte, null */
			return wc;
		}
	}

	/* Convert character byte-by-byte */
	do {
		c = getc_unlocked(f);
		if (c < 0) {
			if (n) {
				f->flags = (f->flags & ~F_EOF) | F_ERR;
				errno = EILSEQ;
			}
			return WEOF;
		}
		b[n++] = c;
		l = mbrtowc(&wc, (void *)&b[n-1], 1, &st);
		if (l == -1) {
			f->flags |= F_ERR;
			while (n) *--f->rpos = b[--n];
			f->flags &= ~F_EOF;
			return WEOF;
		}
	} while (l == -2);

	return wc;
}

wint_t __fgetwc_unlocked(FILE *f)
{
	locale_t *ploc = &CURRENT_LOCALE, loc = *ploc;
	if (f->mode <= 0) fwide(f, 1);
	if ((f->flags & F_WBUF) && f->wget) return f->wget(f);
	*ploc = f->locale;
	wchar_t wc = __fgetwc_unlocked_internal(f);
	*ploc = loc;
	return wc;
}

wint_t fgetwc(FILE *f)
{
	wint_t c;
	FLOCK(f);
	c = __fgetwc_unlocked(f);
	FUNLOCK(f);
	return c;
}

weak_alias(__fgetwc_unlocked, fgetwc_unlocked);
weak_alias(__fgetwc_unlocked, getwc_unlocked);
