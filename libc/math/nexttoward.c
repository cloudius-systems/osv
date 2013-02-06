#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
double nexttoward(double x, long double y)
{
	return nextafter(x, y);
}
#else
#define SIGN ((uint64_t)1<<63)

double nexttoward(double x, long double y)
{
	union dshape ux;
	int e;

	if (isnan(x) || isnan(y))
		return x + y;
	if (x == y)
		return y;
	ux.value = x;
	if (x == 0) {
		ux.bits = 1;
		if (signbit(y))
			ux.bits |= SIGN;
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
	e = ux.bits>>52 & 0x7ff;
	/* raise overflow if ux.value is infinite and x is finite */
	if (e == 0x7ff)
		FORCE_EVAL(x+x);
	/* raise underflow if ux.value is subnormal or zero */
	if (e == 0)
		FORCE_EVAL(x*x + ux.value*ux.value);
	return ux.value;
}
#endif
