/* origin: FreeBSD /usr/src/lib/msun/src/s_floor.c */
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
/*
 * floor(x)
 * Return x rounded toward -inf to integral value
 * Method:
 *      Bit twiddling.
 * Exception:
 *      Inexact flag raised if x not equal to floor(x).
 */

#include "libm.h"

static const double huge = 1.0e300;

double floor(double x)
{
	int32_t i0,i1,j0;
	uint32_t i,j;

	EXTRACT_WORDS(i0, i1, x);
	// FIXME: signed shift
	j0 = ((i0>>20)&0x7ff) - 0x3ff;
	if (j0 < 20) {
		if (j0 < 0) {  /* |x| < 1 */
			/* raise inexact if x != 0 */
			if (huge+x > 0.0) {
				if (i0 >= 0) {  /* x >= 0 */
					i0 = i1 = 0;
				} else if (((i0&0x7fffffff)|i1) != 0) {
					i0 = 0xbff00000;
					i1 = 0;
				}
			}
		} else {
			i = 0x000fffff>>j0;
			if (((i0&i)|i1) == 0)
				return x; /* x is integral */
			 /* raise inexact flag */
			if (huge+x > 0.0) {
				if (i0 < 0)
					i0 += 0x00100000>>j0;
				i0 &= ~i;
				i1 = 0;
			}
		}
	} else if (j0 > 51) {
		if (j0 == 0x400)
			return x+x; /* inf or NaN */
		else
			return x;   /* x is integral */
	} else {
		i = (uint32_t)0xffffffff>>(j0-20);
		if ((i1&i) == 0)
			return x;   /* x is integral */
		/* raise inexact flag */
		if (huge+x > 0.0) {
			if (i0 < 0) {
				if (j0 == 20)
					i0++;
				else {
					j = i1+(1<<(52-j0));
					if (j < i1)
						i0++; /* got a carry */
					i1 = j;
				}
			}
			i1 &= ~i;
		}
	}
	INSERT_WORDS(x, i0, i1);
	return x;
}
