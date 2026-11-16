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
 * Modified for OSv from musl 1.2.1 src/stdio/open_wmemstream.c: the stream
 * is a wide buffer (F_WBUF): wide characters are stored unconverted through
 * wms_wput, and bytes from the narrow formatting paths are stored as
 * wchar_t values, so any wchar_t (WEOF included) round-trips, as with glibc.
 */
#include "stdio_impl.h"
#include <wchar.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include "libc.h"

struct cookie {
	wchar_t **bufp;
	size_t *sizep;
	size_t pos;
	wchar_t *buf;
	size_t len;
	size_t space;
};

struct wms_FILE {
	FILE f;
	struct cookie c;
	unsigned char buf[1];
};

static off_t wms_seek(FILE *f, off_t off, int whence)
{
	ssize_t base;
	struct cookie *c = f->cookie;
	if (whence>2U) {
fail:
		errno = EINVAL;
		return -1;
	}
	base = (size_t [3]){0, c->pos, c->len}[whence];
	if (off < -base || off > SSIZE_MAX/4-base) goto fail;
	return c->pos = base+off;
}

static int wms_grow(struct cookie *c, size_t len)
{
	size_t len2;
	wchar_t *newbuf;
	if (len + c->pos < c->space) return 0;
	len2 = 2*c->space+1 | c->pos+len+1;
	if (len2 > SSIZE_MAX/4) return -1;
	newbuf = realloc(c->buf, len2*4);
	if (!newbuf) return -1;
	*c->bufp = c->buf = newbuf;
	memset(c->buf + c->space, 0, 4*(len2 - c->space));
	c->space = len2;
	return 0;
}

static void wms_advance(struct cookie *c, size_t len)
{
	c->pos += len;
	if (c->pos >= c->len) c->len = c->pos;
	*c->sizep = c->pos;
}

static size_t wms_write(FILE *f, const unsigned char *buf, size_t len)
{
	struct cookie *c = f->cookie;
	size_t i, len2 = f->wpos - f->wbase;
	if (len2) {
		f->wpos = f->wbase;
		if (wms_write(f, f->wbase, len2) < len2) return 0;
	}
	if (wms_grow(c, len)) return 0;
	for (i = 0; i < len; i++) c->buf[c->pos + i] = buf[i];
	wms_advance(c, len);
	return len;
}

static wint_t wms_wput(wchar_t wc, FILE *f)
{
	struct cookie *c = f->cookie;
	size_t n = f->wpos - f->wbase;
	f->wpos = f->wbase;
	if (n && wms_write(f, f->wbase, n) < n) return WEOF;
	if (wms_grow(c, 1)) {
		f->flags |= F_ERR;
		return WEOF;
	}
	c->buf[c->pos] = wc;
	wms_advance(c, 1);
	return wc;
}

static int wms_close(FILE *f)
{
	return 0;
}

FILE *open_wmemstream(wchar_t **bufp, size_t *sizep)
{
	struct wms_FILE *f;
	wchar_t *buf;

	if (!(f=malloc(sizeof *f))) return 0;
	if (!(buf=malloc(sizeof *buf))) {
		free(f);
		return 0;
	}
	memset(&f->f, 0, sizeof f->f);
	memset(&f->c, 0, sizeof f->c);
	f->f.cookie = &f->c;

	f->c.bufp = bufp;
	f->c.sizep = sizep;
	f->c.pos = f->c.len = f->c.space = *sizep = 0;
	f->c.buf = *bufp = buf;
	*buf = 0;

	f->f.flags = F_NORD | F_WBUF;
	f->f.fd = -1;
	f->f.buf = f->buf;
	f->f.buf_size = 0;
	f->f.lbf = EOF;
	f->f.write = wms_write;
	f->f.wput = wms_wput;
	f->f.seek = wms_seek;
	f->f.close = wms_close;

	fwide(&f->f, 1);

	return __ofl_add(&f->f);
}
