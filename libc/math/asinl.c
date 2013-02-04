/* origin: FreeBSD /usr/src/lib/msun/src/e_asinl.c */
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
 * See comments in asin.c.
 * Converted to long double by David Schultz <das@FreeBSD.ORG>.
 */

#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double asinl(long double x)
{
	return asin(x);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
#include "__invtrigl.h"
/* 0.95 */
#define THRESH  ((0xe666666666666666ULL>>(64-(LDBL_MANH_SIZE-1)))|LDBL_NBIT)

long double asinl(long double x)
{
	union IEEEl2bits u;
	long double z,r,s;
	uint16_t expsign, expt;

	u.e = x;
	expsign = u.xbits.expsign;
	expt = expsign & 0x7fff;
	if (expt >= 0x3fff) {   /* |x| >= 1 or nan */
		if (expt == 0x3fff &&
		    ((u.bits.manh&~LDBL_NBIT)|u.bits.manl) == 0)
			/* asin(+-1)=+-pi/2 with inexact */
			return x*pio2_hi + 0x1p-120f;
		return 0/(x-x);
	}
	if (expt < 0x3fff - 1) {  /* |x| < 0.5 */
		if (expt < 0x3fff - 32) {  /* |x|<0x1p-32, asinl(x)=x */
			/* return x with inexact if x!=0 */
			FORCE_EVAL(x + 0x1p120f);
			return x;
		}
		return x + x*__invtrigl_R(x*x);
	}
	/* 1 > |x| >= 0.5 */
	z = (1.0 - fabsl(x))*0.5;
	s = sqrtl(z);
	r = __invtrigl_R(z);
	if (u.bits.manh >= THRESH) { /* if |x| is close to 1 */
		x = pio2_hi - (2*(s+s*r)-pio2_lo);
	} else {
		long double f, c;
		u.e = s;
		u.bits.manl = 0;
		f = u.e;
		c = (z-f*f)/(s+f);
		x = 0.5*pio2_hi-(2*s*r - (pio2_lo-2*c) - (0.5*pio2_hi-2*f));
	}
	if (expsign>>15)
		return -x;
	return x;
}
#endif
