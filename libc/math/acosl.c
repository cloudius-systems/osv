/* origin: FreeBSD /usr/src/lib/msun/src/e_acosl.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
/*
 * See comments in acos.c.
 * Converted to long double by David Schultz <das@FreeBSD.ORG>.
 */

#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double acosl(long double x)
{
	return acos(x);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
#include "__invtrigl.h"

long double acosl(long double x)
{
	union IEEEl2bits u;
	long double z, w, s, c, df;
	int16_t expsign, expt;
	u.e = x;
	expsign = u.xbits.expsign;
	expt = expsign & 0x7fff;
	/* |x| >= 1 or nan */
	if (expt >= 0x3fff) {
		if (expt == 0x3fff &&
			((u.bits.manh & ~LDBL_NBIT) | u.bits.manl) == 0) {
			if (expsign > 0)
				return 0;  /* acos(1) = 0 */
			return 2*pio2_hi + 0x1p-120f;  /* acos(-1)= pi */
		}
		return 0/(x-x);  /* acos(|x|>1) is NaN */
	}
	/* |x| < 0.5 */
	if (expt < 0x3fff - 1) {
		if (expt < 0x3fff - 65)
			return pio2_hi + 0x1p-120f;  /* x < 0x1p-65: acosl(x)=pi/2 */
		return pio2_hi - (x - (pio2_lo - x * __invtrigl_R(x*x)));
	}
	/* x < -0.5 */
	if (expsign < 0) {
		z = (1.0 + x) * 0.5;
		s = sqrtl(z);
		w = __invtrigl_R(z) * s - pio2_lo;
		return 2*(pio2_hi - (s + w));
	}
	/* x > 0.5 */
	z = (1.0 - x) * 0.5;
	s = sqrtl(z);
	u.e = s;
	u.bits.manl = 0;
	df = u.e;
	c = (z - df * df) / (s + df);
	w = __invtrigl_R(z) * s + c;
	return 2*(df + w);
}
#endif
