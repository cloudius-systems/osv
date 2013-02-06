#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double nextafterl(long double x, long double y)
{
	return nextafter(x, y);
}
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
#define MSB ((uint64_t)1<<63)
long double nextafterl(long double x, long double y)
{
	union ldshape ux, uy;

	if (isnan(x) || isnan(y))
		return x + y;
	if (x == y)
		return y;
	ux.value = x;
	if (x == 0) {
		uy.value = y;
		ux.bits.m = 1;
		ux.bits.sign = uy.bits.sign;
	} else if (x < y ^ ux.bits.sign) {
		ux.bits.m++;
		if ((ux.bits.m & ~MSB) == 0) {
			ux.bits.m = MSB;
			ux.bits.exp++;
		}
	} else {
		if ((ux.bits.m & ~MSB) == 0) {
			ux.bits.exp--;
			if (ux.bits.exp)
				ux.bits.m = 0;
		}
		ux.bits.m--;
	}
	/* raise overflow if ux.value is infinite and x is finite */
	if (ux.bits.exp == 0x7fff)
		return x + x;
	/* raise underflow if ux.value is subnormal or zero */
	if (ux.bits.exp == 0)
		FORCE_EVAL(x*x + ux.value*ux.value);
	return ux.value;
}
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384
long double nextafterl(long double x, long double y)
{
	union ldshape ux, uy;

	if (isnan(x) || isnan(y))
		return x + y;
	if (x == y)
		return y;
	ux.value = x;
	if (x == 0) {
		uy.value = y;
		ux.bits.mlo = 1;
		ux.bits.sign = uy.bits.sign;
	} else if (x < y ^ ux.bits.sign) {
		ux.bits.mlo++;
		if (ux.bits.mlo == 0) {
			ux.bits.mhi++;
			if (ux.bits.mhi == 0)
				ux.bits.exp++;
		}
	} else {
		if (ux.bits.mlo == 0) {
			if (ux.bits.mhi == 0)
				ux.bits.exp--;
			ux.bits.mhi--;
		}
		ux.bits.mlo--;
	}
	/* raise overflow if ux.value is infinite and x is finite */
	if (ux.bits.exp == 0x7fff)
		return x + x;
	/* raise underflow if ux.value is subnormal or zero */
	if (ux.bits.exp == 0)
		FORCE_EVAL(x*x + ux.value*ux.value);
	return ux.value;
}
#endif
