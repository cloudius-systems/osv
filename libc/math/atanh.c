#include "libm.h"

/* atanh(x) = log((1+x)/(1-x))/2 = log1p(2x/(1-x))/2 ~= x + x^3/3 + o(x^5) */
double atanh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	unsigned e = u.i >> 52 & 0x7ff;
	unsigned s = u.i >> 63;

	/* |x| */
	u.i &= (uint64_t)-1/2;
	x = u.f;

	if (e < 0x3ff - 1) {
		/* |x| < 0.5, up to 1.7ulp error */
		x = 0.5*log1p(2*x + 2*x*x/(1-x));
	} else {
		x = 0.5*log1p(2*x/(1-x));
	}
	return s ? -x : x;
}
