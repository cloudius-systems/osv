#include <stdlib.h>
#include <string.h>
#include "locale_impl.h"
#include "libc.h"

locale_t __newlocale(int mask, const char *name, locale_t base)
{
	if (*name && strcmp(name, "C") && strcmp(name, "POSIX"))
		return 0;
	if (!base) base = calloc(1, sizeof *base);
	return base;
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__newlocale, newlocale);
