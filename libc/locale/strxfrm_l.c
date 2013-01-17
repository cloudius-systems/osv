#include <string.h>
#include "libc.h"

size_t __strxfrm_l(char *restrict dest, const char *restrict src, size_t n, locale_t l)
{
	return strxfrm(dest, src, n);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__strxfrm_l, strxfrm_l);
