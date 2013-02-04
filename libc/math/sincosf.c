/* origin: FreeBSD /usr/src/lib/msun/src/s_sinf.c */
/*
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 * Optimized by Bruce D. Evans.
 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#include "libm.h"

/* Small multiples of pi/2 rounded to double precision. */
static const double
s1pio2 = 1*M_PI_2, /* 0x3FF921FB, 0x54442D18 */
s2pio2 = 2*M_PI_2, /* 0x400921FB, 0x54442D18 */
s3pio2 = 3*M_PI_2, /* 0x4012D97C, 0x7F3321D2 */
s4pio2 = 4*M_PI_2; /* 0x401921FB, 0x54442D18 */

void sincosf(float x, float *sin, float *cos)
{
	double y, s, c;
	uint32_t n, hx, ix;

	GET_FLOAT_WORD(hx, x);
	ix = hx & 0x7fffffff;

	/* |x| ~<= pi/4 */
	if (ix <= 0x3f490fda) {
		/* |x| < 2**-12 */
		if (ix < 0x39800000) {
			/* raise inexact if x != 0 */
			if((int)x == 0) {
				*sin = x;
				*cos = 1.0f;
			}
			return;
		}
		*sin = __sindf(x);
		*cos = __cosdf(x);
		return;
	}

	/* |x| ~<= 5*pi/4 */
	if (ix <= 0x407b53d1) {
		if (ix <= 0x4016cbe3) {  /* |x| ~<= 3pi/4 */
			if (hx < 0x80000000) {
				*sin = __cosdf(x - s1pio2);
				*cos = __sindf(s1pio2 - x);
			} else {
				*sin = -__cosdf(x + s1pio2);
				*cos = __sindf(x + s1pio2);
			}
			return;
		}
		*sin = __sindf(hx < 0x80000000 ? s2pio2 - x : -s2pio2 - x);
		*cos = -__cosdf(hx < 0x80000000 ? x - s2pio2 : x + s2pio2);
		return;
	}

	/* |x| ~<= 9*pi/4 */
	if (ix <= 0x40e231d5) {
		if (ix <= 0x40afeddf) {  /* |x| ~<= 7*pi/4 */
			if (hx < 0x80000000) {
				*sin = -__cosdf(x - s3pio2);
				*cos = __sindf(x - s3pio2);
			} else {
				*sin = __cosdf(x + s3pio2);
				*cos = __sindf(-s3pio2 - x);
			}
			return;
		}
		*sin = __sindf(hx < 0x80000000 ? x - s4pio2 : x + s4pio2);
		*cos = __cosdf(hx < 0x80000000 ? x - s4pio2 : x + s4pio2);
		return;
	}

	/* sin(Inf or NaN) is NaN */
	if (ix >= 0x7f800000) {
		*sin = *cos = x - x;
		return;
	}

	/* general argument reduction needed */
	n = __rem_pio2f(x, &y);
	s = __sindf(y);
	c = __cosdf(y);
	switch (n&3) {
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
