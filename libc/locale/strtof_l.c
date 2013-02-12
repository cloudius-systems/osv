#include <locale.h>
#include <stdlib.h>
#include <wchar.h>
#include "libc.h"

float __strtof_l(const char *restrict s, char **restrict p, locale_t l)
{
        return strtof(s, p);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__strtof_l, strtof_l);
