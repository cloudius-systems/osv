#include <wctype.h>
#include "libc.h"

wint_t __towupper_l(wint_t c, locale_t l)
{
	return towupper(c);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__towupper_l, towupper_l);
