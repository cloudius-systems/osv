/* origin: FreeBSD /usr/src/lib/msun/src/e_log10f.c */
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
 * See comments in log10.c.
 */

#include "libm.h"
#include "__log1pf.h"

static const float
two25     =  3.3554432000e+07, /* 0x4c000000 */
ivln10hi  =  4.3432617188e-01, /* 0x3ede6000 */
ivln10lo  = -3.1689971365e-05, /* 0xb804ead9 */
log10_2hi =  3.0102920532e-01, /* 0x3e9a2080 */
log10_2lo =  7.9034151668e-07; /* 0x355427db */

float log10f(float x)
{
	float f,hfsq,hi,lo,r,y;
	int32_t i,k,hx;

	GET_FLOAT_WORD(hx, x);

	k = 0;
	if (hx < 0x00800000) {  /* x < 2**-126  */
		if ((hx&0x7fffffff) == 0)
			return -two25/0.0f;  /* log(+-0)=-inf */
		if (hx < 0)
			return (x-x)/0.0f;   /* log(-#) = NaN */
		/* subnormal number, scale up x */
		k -= 25;
		x *= two25;
		GET_FLOAT_WORD(hx, x);
	}
	if (hx >= 0x7f800000)
		return x+x;
	if (hx == 0x3f800000)
		return 0.0f;  /* log(1) = +0 */
	k += (hx>>23) - 127;
	hx &= 0x007fffff;
	i = (hx+(0x4afb0d))&0x800000;
	SET_FLOAT_WORD(x, hx|(i^0x3f800000));  /* normalize x or x/2 */
	k += i>>23;
	y = (float)k;
	f = x - 1.0f;
	hfsq = 0.5f * f * f;
	r = __log1pf(f);

// FIXME
//      /* See log2f.c and log2.c for details. */
//      if (sizeof(float_t) > sizeof(float))
//              return (r - hfsq + f) * ((float_t)ivln10lo + ivln10hi) +
//                  y * ((float_t)log10_2lo + log10_2hi);
	hi = f - hfsq;
	GET_FLOAT_WORD(hx, hi);
	SET_FLOAT_WORD(hi, hx&0xfffff000);
	lo = (f - hi) - hfsq + r;
	return y*log10_2lo + (lo+hi)*ivln10lo + lo*ivln10hi +
	        hi*ivln10hi + y*log10_2hi;
}
