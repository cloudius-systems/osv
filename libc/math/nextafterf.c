#include "libm.h"

#define SIGN 0x80000000

float nextafterf(float x, float y)
{
	union fshape ux, uy;
	uint32_t ax, ay, e;

	if (isnan(x) || isnan(y))
		return x + y;
	ux.value = x;
	uy.value = y;
	if (ux.bits == uy.bits)
		return y;
	ax = ux.bits & ~SIGN;
	ay = uy.bits & ~SIGN;
	if (ax == 0) {
		if (ay == 0)
			return y;
		ux.bits = (uy.bits & SIGN) | 1;
	} else if (ax > ay || ((ux.bits ^ uy.bits) & SIGN))
		ux.bits--;
	else
		ux.bits++;
	e = ux.bits & 0x7f800000;
	/* raise overflow if ux.value is infinite and x is finite */
	if (e == 0x7f800000)
		FORCE_EVAL(x+x);
	/* raise underflow if ux.value is subnormal or zero */
	if (e == 0)
		FORCE_EVAL(x*x + ux.value*ux.value);
	return ux.value;
}
