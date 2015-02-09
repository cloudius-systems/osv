#include <stdlib.h>
#include "locale_impl.h"
#include "libc.h"

void __freelocale(locale_t l)
{
    // In our implementation, newlocale() might return c_locale instead of
    // allocating a new copy of the locale... We can't free that.
    extern locale_t __c_locale_ptr;
    if (l == __c_locale_ptr) {
        return;
    }

	free(l);
}

/* OSv local: a libstdc++ build against glibc wants the __ version */
weak_alias(__freelocale, freelocale);
