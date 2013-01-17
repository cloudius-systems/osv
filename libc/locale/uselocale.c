#include "locale_impl.h"
#include "libc.h"

static __thread locale_t __locale;

locale_t __uselocale(locale_t l)
{
	locale_t old = __locale;
	if (l) __locale = l;
	return old;
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__uselocale, uselocale);
