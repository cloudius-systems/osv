#include <stdlib.h>
#include "locale_impl.h"
#include "libc.h"

void __freelocale(locale_t l)
{
	free(l);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__freelocale, freelocale);
