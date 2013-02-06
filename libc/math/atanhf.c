#include "libm.h"

/* atanh(x) = log((1+x)/(1-x))/2 = log1p(2x/(1-x))/2 ~= x + x^3/3 + o(x^5) */
float atanhf(float x)
{
	union {float f; uint32_t i;} u = {.f = x};
	unsigned s = u.i >> 31;

	/* |x| */
	u.i &= 0x7fffffff;
	x = u.f;

	if (u.i < 0x3f800000 - (1<<23)) {
		/* |x| < 0.5, up to 1.7ulp error */
		x = 0.5f*log1pf(2*x + 2*x*x/(1-x));
	} else {
		x = 0.5f*log1pf(2*x/(1-x));
	}
	return s ? -x : x;
}
