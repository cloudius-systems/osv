#include <stdlib.h>
#include <string.h>
#include "locale_impl.h"
#include "libc.h"

locale_t __duplocale(locale_t old)
{
	locale_t new;
	new = calloc(1, sizeof *new);
	if (new && old != LC_GLOBAL_LOCALE) memcpy(new, old, sizeof *new);
	return new;
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__duplocale, duplocale);
