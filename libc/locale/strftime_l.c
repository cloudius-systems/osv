#include <locale.h>
#include <time.h>
#include "libc.h"

size_t __strftime_l(char *restrict s, size_t n, const char *restrict f, const struct tm *restrict tm, locale_t l)
{
	return strftime(s, n, f, tm);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__strftime_l, strftime_l);
