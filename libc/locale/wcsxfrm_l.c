#include <wchar.h>
#include "libc.h"

size_t __wcsxfrm_l(wchar_t *restrict dest, const wchar_t *restrict src, size_t n, locale_t locale)
{
	return wcsxfrm(dest, src, n);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__wcsxfrm_l, wcsxfrm_l);
