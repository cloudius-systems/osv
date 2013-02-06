#define _GNU_SOURCE
#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
void sincosl(long double x, long double *sin, long double *cos)
{
	sincos(x, (double *)sin, (double *)cos);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
void sincosl(long double x, long double *sin, long double *cos)
{
	union IEEEl2bits u;
	int n;
	long double y[2], s, c;

	u.e = x;
	u.bits.sign = 0;

	/* x = +-0 or subnormal */
	if (!u.bits.exp) {
		*sin = x;
		*cos = 1.0;
		return;
	}

	/* x = nan or inf */
	if (u.bits.exp == 0x7fff) {
		*sin = *cos = x - x;
		return;
	}

	/* |x| < pi/4 */
	if (u.e < M_PI_4) {
		*sin = __sinl(x, 0, 0);
		*cos = __cosl(x, 0);
		return;
	}

	n = __rem_pio2l(x, y);
	s = __sinl(y[0], y[1], 1);
	c = __cosl(y[0], y[1]);
	switch (n & 3) {
	case 0:
		*sin = s;
		*cos = c;
		break;
	case 1:
		*sin = c;
		*cos = -s;
		break;
	case 2:
		*sin = -s;
		*cos = -c;
		break;
	case 3:
	default:
		*sin = -c;
		*cos = s;
		break;
	}
}
#endif
