#include <locale.h>
#include <stdlib.h>
#include <wchar.h>
#include "libc.h"
#include "osv-numeric.h"

float __strtof_l(const char *restrict s, char **restrict p, locale_t l)
{
        return __osv_strtox(s, p, 0, l);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__strtof_l, strtof_l);
