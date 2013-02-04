#include "libm.h"

// FIXME: should be a macro
#if (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
int __signbitl(long double x)
{
	union ldshape u = {x};

	return u.bits.sign;
}
#endif
