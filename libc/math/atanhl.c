#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double atanhl(long double x)
{
	return atanh(x);
}
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
/* atanh(x) = log((1+x)/(1-x))/2 = log1p(2x/(1-x))/2 ~= x + x^3/3 + o(x^5) */
long double atanhl(long double x)
{
	union {
		long double f;
		struct{uint64_t m; uint16_t se; uint16_t pad;} i;
	} u = {.f = x};
	unsigned e = u.i.se & 0x7fff;
	unsigned s = u.i.se >> 15;

	/* |x| */
	u.i.se = e;
	x = u.f;

	if (e < 0x3fff - 1) {
		/* |x| < 0.5, up to 1.7ulp error */
		x = 0.5*log1pl(2*x + 2*x*x/(1-x));
	} else {
		x = 0.5*log1pl(2*x/(1-x));
	}
	return s ? -x : x;
}
#endif
