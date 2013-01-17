#include <string.h>
#include <locale.h>
#include "libc.h"

int __strcoll_l(const char *l, const char *r, locale_t loc)
{
	return strcoll(l, r);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__strcoll_l, strcoll_l);
