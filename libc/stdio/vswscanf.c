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
 * Modified for OSv from musl 1.2.1 src/stdio/vswscanf.c: the stream is a
 * wide buffer (F_WBUF): the byte buffer holds only the ASCII characters
 * ahead (copied as bytes, for the narrow scanning paths), and any other
 * character is returned unconverted by ws_wget, so any wchar_t
 * round-trips, as with glibc.
 */
#define _GNU_SOURCE // avoid __REDIRECT(vswscanf) in glibc headers
#include "stdio_impl.h"
#include "libc.h"
#include <wchar.h>

struct cookie {
	const wchar_t *s, *start;
};

static size_t ws_fill(FILE *f)
{
	struct cookie *c = f->cookie;
	size_t k = 0;

	while (k < f->buf_size && *c->s && (unsigned)*c->s < 128)
		f->buf[k++] = *c->s++;
	f->rpos = f->buf;
	f->rend = f->buf + k;
	return k;
}

static size_t wstring_read(FILE *f, unsigned char *buf, size_t len)
{
	if (!ws_fill(f) || !len) return 0;
	*buf = *f->rpos++;
	return 1;
}

static wint_t ws_wget(FILE *f)
{
	struct cookie *c = f->cookie;

	if (f->rpos != f->rend || ws_fill(f)) return *f->rpos++;
	if (!*c->s) return WEOF;
	return *c->s++;
}

/* vfwscanf pushes back ASCII through rpos itself: this is the character
 * ws_wget just returned from the string */
static wint_t ws_wunget(wint_t wc, FILE *f)
{
	struct cookie *c = f->cookie;

	if (c->s == c->start || c->s[-1] != wc) return WEOF;
	c->s--;
	return wc;
}

int vswscanf(const wchar_t *restrict s, const wchar_t *restrict fmt, va_list ap)
{
	unsigned char buf[256];
	struct cookie c = { s, s };
	FILE f = {
		.flags = F_WBUF,
		.buf = buf, .buf_size = sizeof buf,
		.cookie = &c,
		.read = wstring_read, .no_locking = true,
		.wget = ws_wget, .wunget = ws_wunget,
	};
	return vfwscanf(&f, fmt, ap);
}

weak_alias(vswscanf,__isoc99_vswscanf);
