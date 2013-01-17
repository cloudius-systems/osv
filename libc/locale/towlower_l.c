#include <wctype.h>
#include "libc.h"

wint_t __towlower_l(wint_t c, locale_t l)
{
	return towlower(c);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__towlower_l, towlower_l);
