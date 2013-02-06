#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double scalbnl(long double x, int n)
{
	return scalbn(x, n);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
long double scalbnl(long double x, int n)
{
	union IEEEl2bits scale;

	if (n > 16383) {
		x *= 0x1p16383L;
		n -= 16383;
		if (n > 16383) {
			x *= 0x1p16383L;
			n -= 16383;
			if (n > 16383)
				return x * 0x1p16383L;
		}
	} else if (n < -16382) {
		x *= 0x1p-16382L;
		n += 16382;
		if (n < -16382) {
			x *= 0x1p-16382L;
			n += 16382;
			if (n < -16382)
				return x * 0x1p-16382L;
		}
	}
	scale.e = 1.0;
	scale.bits.exp = 0x3fff + n;
	return x * scale.e;
}
#endif
