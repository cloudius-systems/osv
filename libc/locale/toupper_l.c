#include <ctype.h>
#include "libc.h"

#undef __toupper_l
int __toupper_l(int c, locale_t l)
{
	return toupper(c);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__toupper_l, toupper_l);
