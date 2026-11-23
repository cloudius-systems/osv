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
 * Modified for OSv from musl 1.2.1 src/stdio/ungetwc.c: wide-buffer streams
 * (F_WBUF) push back through f->wunget; on a byte-backed stream a character
 * the stream locale cannot encode is pushed back as 0xff and its wchar_t
 * bytes (fgetwc.c returns it), so ungetwc() of any character succeeds, as
 * glibc's does.
 */
#include "stdio_impl.h"
#include "locale_impl.h"
#include <wchar.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

wint_t ungetwc(wint_t c, FILE *f)
{
	unsigned char mbc[MB_LEN_MAX], *old;
	int l, mark = 0, e = errno;
	wchar_t wc = c;
	locale_t *ploc = &CURRENT_LOCALE, loc = *ploc;

	FLOCK(f);

	if (f->mode <= 0) fwide(f, 1);
	if (f->flags & F_WBUF) {
		c = c != WEOF && f->wunget ? f->wunget(c, f) : WEOF;
		FUNLOCK(f);
		return c;
	}
	*ploc = f->locale;

	if (!f->rpos) __toread(f);
	if (c != WEOF && (l = wcrtomb((void *)mbc, c, 0)) < 0) {
		errno = e;
		mark = 1;
		l = 1 + sizeof wc;
	}
	if (!f->rpos || c == WEOF || f->rpos < f->buf - UNGET + l) {
		FUNLOCK(f);
		*ploc = loc;
		return WEOF;
	}

	/* [wpb_lo, wpb_hi) spans the pushback since the first marked one.
	 * A seek or fflush clears it at the next fgetwc or ungetwc; only a
	 * byte read in between (undefined on a wide stream) could find it
	 * stale over new data. */
	old = f->rpos;
	if (old >= f->wpb_hi) f->wpb_lo = f->wpb_hi = 0;
	if (mark) {
		f->rpos -= l;
		f->rpos[0] = 0xff;
		memcpy(f->rpos + 1, &wc, sizeof wc);
		if (!f->wpb_hi) f->wpb_hi = old;
	} else if (isascii(c)) *--f->rpos = c;
	else memcpy(f->rpos -= l, mbc, l);
	if (f->wpb_hi) f->wpb_lo = f->rpos;

	f->flags &= ~F_EOF;

	FUNLOCK(f);
	*ploc = loc;
	return c;
}
