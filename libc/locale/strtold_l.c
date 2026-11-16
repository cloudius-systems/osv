#include <locale.h>
#include <stdlib.h>
#include <wchar.h>
#include "libc.h"
#include "osv-numeric.h"

long double __strtold_l(const char *restrict s, char **restrict p, locale_t l)
{
        return __osv_strtox(s, p, 2, l);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__strtold_l, strtold_l);
