#include <limits.h>
#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
int ilogbl(long double x)
{
	return ilogb(x);
}
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
int ilogbl(long double x)
{
	union ldshape u = {x};
	uint64_t m = u.bits.m;
	int e = u.bits.exp;

	if (!e) {
		if (m == 0) {
			FORCE_EVAL(0/0.0f);
			return FP_ILOGB0;
		}
		/* subnormal x */
		for (e = -0x3fff+1; m < (uint64_t)1<<63; e--, m<<=1);
		return e;
	}
	if (e == 0x7fff) {
		FORCE_EVAL(0/0.0f);
		/* in ld80 msb is set in inf */
		return m & (uint64_t)-1>>1 ? FP_ILOGBNAN : INT_MAX;
	}
	return e - 0x3fff;
}
#endif
