#include <wchar.h>
#include "libc.h"

int __wcscoll_l(const wchar_t *l, const wchar_t *r, locale_t locale)
{
	return wcscoll(l, r);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__wcscoll_l, wcscoll_l);
