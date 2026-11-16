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
 * Modified for OSv from musl 1.2.1 src/stdio/fputws.c: a loop over
 * __fputwc_unlocked(), so that wide-buffer streams store the characters
 * unconverted and byte-backed ones get its transliteration or '?' (which
 * wcsrtombs() cannot do); returns 1 on success, as glibc does.
 */
#include "stdio_impl.h"
#include <wchar.h>

wint_t __fputwc_unlocked(wchar_t, FILE *);

int fputws(const wchar_t *restrict ws, FILE *restrict f)
{
	int r = 1;

	FLOCK(f);

	fwide(f, 1);

	for (; ws && *ws; ws++)
		if (__fputwc_unlocked(*ws, f) == WEOF) {
			r = -1;
			break;
		}

	FUNLOCK(f);

	return r;
}

weak_alias(fputws, fputws_unlocked);
