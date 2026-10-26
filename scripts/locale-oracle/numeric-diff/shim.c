/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 * Linux stand-ins for the OSv stdio internals OSv's printf and scanf code calls (as musl's do
 * them), and osv_snprintf/osv_sscanf/... entry points for ndiff.c. Built with the shim. */
#include "stdio_impl.h"
#include <langinfo.h>
#include <string.h>
char *osv_shim_nl_langinfo_l(nl_item it, locale_t l)
{
	if (!l || l == LC_GLOBAL_LOCALE) return nl_langinfo(it);
	return nl_langinfo_l(it, l);
}
int __towrite(FILE *f)
{
	f->mode |= f->mode-1;
	if (f->flags & F_NOWR) {
		f->flags |= F_ERR;
		return EOF;
	}
	f->rpos = f->rend = 0;
	f->wpos = f->wbase = f->buf;
	f->wend = f->buf + f->buf_size;
	return 0;
}
int __toread(FILE *f)
{
	f->mode |= f->mode-1;
	if (f->wpos != f->wbase) f->write(f, 0, 0);
	f->wpos = f->wbase = f->wend = 0;
	if (f->flags & F_NORD) {
		f->flags |= F_ERR;
		return EOF;
	}
	f->rpos = f->rend = f->buf + f->buf_size;
	return (f->flags & F_EOF) ? EOF : 0;
}
int __uflow(FILE *f)
{
	unsigned char c;
	if (!__toread(f) && f->read(f, &c, 1)==1) return c;
	return EOF;
}
size_t __fwritex(const unsigned char *s, size_t l, FILE *f)
{
	if (!f->wend && __towrite(f)) return 0;
	if (l > (size_t)(f->wend - f->wpos)) return f->write(f, s, l);
	memcpy(f->wpos, s, l);
	f->wpos += l;
	return l;
}
size_t __string_read(FILE *f, unsigned char *buf, size_t len)
{
	char *src = f->cookie;
	size_t k = len + 256;
	char *end = memchr(src, 0, k);
	if (end) k = end - src;
	if (k < len) len = k;
	memcpy(buf, src, len);
	f->rpos = (void *)(src + len);
	f->rend = (void *)(src + k);
	f->cookie = src + k;
	return len;
}
/* Only wide-buffer streams (swprintf, swscanf) are used. */
wint_t osv_fputwc(wchar_t c, FILE *f)
{
	if (f->flags & F_WBUF) return f->wput(c, f);
	f->flags |= F_ERR;
	return WEOF;
}
wint_t osv_getwc(FILE *f)
{
	if ((f->flags & F_WBUF) && f->wget) return f->wget(f);
	return WEOF;
}
wint_t osv_ungetwc(wint_t c, FILE *f)
{
	if ((f->flags & F_WBUF) && f->wunget && c != WEOF) return f->wunget(c, f);
	return WEOF;
}
int osv_fwide(FILE *f, int m)
{
	(void)f;
	return m;
}
int osv_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = osv_vfprintf(f, fmt, ap);
	va_end(ap);
	return r;
}
int osv_fscanf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = osv_vfscanf(f, fmt, ap);
	va_end(ap);
	return r;
}
int osv_snprintf(char *s, size_t n, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = osv_vsnprintf(s, n, fmt, ap);
	va_end(ap);
	return r;
}
int osv_swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = osv_vswprintf(s, n, fmt, ap);
	va_end(ap);
	return r;
}
int osv_sscanf(const char *s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = osv_vsscanf(s, fmt, ap);
	va_end(ap);
	return r;
}
int osv_swscanf(const wchar_t *s, const wchar_t *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = osv_vswscanf(s, fmt, ap);
	va_end(ap);
	return r;
}
