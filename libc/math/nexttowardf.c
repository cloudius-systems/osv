#include "libm.h"

float nexttowardf(float x, long double y)
{
	union fshape ux;
	uint32_t e;

	if (isnan(x) || isnan(y))
		return x + y;
	if (x == y)
		return y;
	ux.value = x;
	if (x == 0) {
		ux.bits = 1;
		if (signbit(y))
			ux.bits |= 0x80000000;
	} else if (x < y) {
		if (signbit(x))
			ux.bits--;
		else
			ux.bits++;
	} else {
		if (signbit(x))
			ux.bits++;
		else
			ux.bits--;
	}
	e = ux.bits & 0x7f800000;
	/* raise overflow if ux.value is infinite and x is finite */
	if (e == 0x7f800000)
		FORCE_EVAL(x+x);
	/* raise underflow if ux.value is subnormal or zero */
	if (e == 0)
		FORCE_EVAL(x*x + ux.value*ux.value);
	return ux.value;
}
