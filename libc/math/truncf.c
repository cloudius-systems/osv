/* origin: FreeBSD /usr/src/lib/msun/src/s_truncf.c */
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
 * truncf(x)
 * Return x rounded toward 0 to integral value
 * Method:
 *      Bit twiddling.
 * Exception:
 *      Inexact flag raised if x not equal to truncf(x).
 */

#include "libm.h"

static const float huge = 1.0e30f;

float truncf(float x)
{
	int32_t i0,j0;
	uint32_t i;

	GET_FLOAT_WORD(i0, x);
	j0 = ((i0>>23)&0xff) - 0x7f;
	if (j0 < 23) {
		if (j0 < 0) {  /* |x|<1, return 0*sign(x) */
			/* raise inexact if x != 0 */
			if (huge+x > 0.0f)
				i0 &= 0x80000000;
		} else {
			i = 0x007fffff>>j0;
			if ((i0&i) == 0)
				return x; /* x is integral */
			/* raise inexact */
			if (huge+x > 0.0f)
				i0 &= ~i;
		}
	} else {
		if (j0 == 0x80)
			return x + x;  /* inf or NaN */
		return x;              /* x is integral */
	}
	SET_FLOAT_WORD(x, i0);
	return x;
}
