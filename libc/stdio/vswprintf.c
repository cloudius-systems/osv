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
 * Modified for OSv from musl 1.2.1 src/stdio/vswprintf.c: the stream is a
 * wide buffer (F_WBUF): wide characters are stored unconverted through
 * sw_wput, and bytes from the narrow formatting paths are stored as
 * wchar_t values, so any wchar_t round-trips, as with glibc.
 */
#include "stdio_impl.h"
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

struct cookie {
	wchar_t *ws;
	size_t l;
};

static size_t sw_write(FILE *f, const unsigned char *s, size_t l)
{
	size_t l0 = l;
	struct cookie *c = f->cookie;
	if (s!=f->wbase && sw_write(f, f->wbase, f->wpos-f->wbase)==-1)
		return -1;
	for (; c->l && l; l--, c->l--) *c->ws++ = *s++;
	*c->ws = 0;
	f->wend = f->buf + f->buf_size;
	f->wpos = f->wbase = f->buf;
	return l0;
}

static wint_t sw_wput(wchar_t wc, FILE *f)
{
	struct cookie *c = f->cookie;
	if (f->wpos != f->wbase) sw_write(f, f->wbase, f->wpos-f->wbase);
	if (c->l) {
		*c->ws++ = wc;
		*c->ws = 0;
		c->l--;
	}
	return wc;
}

int vswprintf(wchar_t *restrict s, size_t n, const wchar_t *restrict fmt, va_list ap)
{
	int r;
	unsigned char buf[256];
	struct cookie c = { s, n-1 };
	FILE f = {
		.flags = F_WBUF,
		.lbf = EOF,
		.write = sw_write,
		.no_locking = true,
		.buf = buf,
		.buf_size = sizeof buf,
		.cookie = &c,
		.wput = sw_wput,
	};

	if (!n) {
		return -1;
	} else if (n > INT_MAX) {
		errno = EOVERFLOW;
		return -1;
	}
	r = vfwprintf(&f, fmt, ap);
	sw_write(&f, 0, 0);
	return r>=n ? -1 : r;
}
