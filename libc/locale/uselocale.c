#include "locale_impl.h"
#include "libc.h"

OSV_HIDDEN
__thread locale_t __current_locale;

locale_t __uselocale(locale_t l)
{
	locale_t old = __current_locale;
	if (l) __current_locale = l;
	return old;
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__uselocale, uselocale);
