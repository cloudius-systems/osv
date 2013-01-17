#include <wctype.h>
#include "libc.h"

#undef iswctype_l
int __iswctype_l(wint_t c, wctype_t t, locale_t l)
{
	return iswctype(c, t);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__iswctype_l, iswctype_l);
