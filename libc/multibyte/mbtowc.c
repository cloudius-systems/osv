/* 
 * This code was written by Rich Felker in 2010; no copyright is claimed.
 * This code is in the public domain. Attribution is appreciated but
 * unnecessary.
 */

#include <stdlib.h>
#include <inttypes.h>
#include <wchar.h>
#include <errno.h>

#include "internal.h"

int mbtowc(wchar_t *restrict wc, const char *restrict s, size_t n)
{
	mbstate_t st = { 0 };
	n = mbrtowc(wc, s, n, &st);
	return n+2 ? n : -1;
}
