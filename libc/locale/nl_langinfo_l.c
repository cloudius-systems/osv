#include <locale.h>
#include <langinfo.h>
#include "libc.h"

char *__nl_langinfo_l(nl_item item, locale_t l)
{
	return nl_langinfo(item);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__nl_langinfo_l, nl_langinfo_l);
