#include <locale.h>
#include <stdlib.h>
#include <wchar.h>
#include "libc.h"

double __strtod_l(const char *restrict s, char **restrict p, locale_t l)
{
        return strtod(s, p);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__strtod_l, strtod_l);
