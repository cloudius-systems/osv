#include "libm.h"

/* tanh(x) = (exp(x) - exp(-x))/(exp(x) + exp(-x))
 *         = (exp(2*x) - 1)/(exp(2*x) - 1 + 2)
 *         = (1 - exp(-2*x))/(exp(-2*x) - 1 + 2)
 */
double tanh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	uint32_t w;
	int sign;
	double t;

	/* x = |x| */
	sign = u.i >> 63;
	u.i &= (uint64_t)-1/2;
	x = u.f;
	w = u.i >> 32;

	if (w > 0x3fe193ea) {
		/* |x| > log(3)/2 ~= 0.5493 or nan */
		if (w > 0x40340000) {
			/* |x| > 20 or nan */
			/* note: this branch avoids raising overflow */
			/* raise inexact if x!=+-inf and handle nan */
			t = 1 + 0/(x + 0x1p-120f);
		} else {
			t = expm1(2*x);
			t = 1 - 2/(t+2);
		}
	} else if (w > 0x3fd058ae) {
		/* |x| > log(5/3)/2 ~= 0.2554 */
		t = expm1(2*x);
		t = t/(t+2);
	} else {
		/* |x| is small, up to 2ulp error in [0.1,0.2554] */
		t = expm1(-2*x);
		t = -t/(t+2);
	}
	return sign ? -t : t;
}
