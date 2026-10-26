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
 * Modified for OSv from musl 1.2.1 src/stdlib/strtod.c: the decimal point
 * is the LC_NUMERIC one of the current locale, or of l for the _l variants
 * (libc/locale/strto*_l.c), as glibc 2.41's (measured).
 */
#include <stdlib.h>
#include "../stdio/shgetc.h"
#include "floatscan.h"
#include "../stdio/stdio_impl.h"
#include "osv-numeric.h"

/* l: NULL for the current locale */
long double __osv_strtox(const char *s, char **p, int prec, locale_t l)
{
	struct osv_num nm;
	FILE f = {
		.buf = (void *)s, .rpos = (void *)s,
		.rend = (void *)-1, .no_locking = true,
	};
	__osv_num(&nm, l, 0);
	shlim(&f, 0);
	long double y = __osv_floatscan(&f, prec, 1, &nm, 0);
	off_t cnt = shcnt(&f);
	if (p) *p = cnt ? (char *)s + cnt : (char *)s;
	return y;
}

float strtof(const char *restrict s, char **restrict p)
{
	return __osv_strtox(s, p, 0, 0);
}

double strtod(const char *restrict s, char **restrict p)
{
	return __osv_strtox(s, p, 1, 0);
}

long double strtold(const char *restrict s, char **restrict p)
{
	return __osv_strtox(s, p, 2, 0);
}
