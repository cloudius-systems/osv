#include <wctype.h>
#include "libc.h"

wctype_t __wctype_l(const char *s, locale_t l)
{
	return wctype(s);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__wctype_l, wctype_l);
