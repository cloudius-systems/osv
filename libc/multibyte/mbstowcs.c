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

size_t mbstowcs(wchar_t *restrict ws, const char *restrict s, size_t wn)
{
	mbstate_t st = { 0 };
	return mbsrtowcs(ws, (void*)&s, wn, &st);
}
