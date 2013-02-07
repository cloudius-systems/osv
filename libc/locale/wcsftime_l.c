#include <wchar.h>
#include <time.h>

#include "libc.h"

size_t __wcsftime_l(wchar_t *restrict wcs, size_t n, const wchar_t *restrict f,
		const struct tm *restrict tm, locale_t loc)
{
	return wcsftime(wcs, n, f, tm);
}
weak_alias(__wcsftime_l, wcsftime_l);
