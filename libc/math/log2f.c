/* origin: FreeBSD /usr/src/lib/msun/src/e_log2f.c */
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
 * See comments in log2.c.
 */

#include "libm.h"
#include "__log1pf.h"

static const float
two25   =  3.3554432000e+07, /* 0x4c000000 */
ivln2hi =  1.4428710938e+00, /* 0x3fb8b000 */
ivln2lo = -1.7605285393e-04; /* 0xb9389ad4 */

float log2f(float x)
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

	/*
	 * We no longer need to avoid falling into the multi-precision
	 * calculations due to compiler bugs breaking Dekker's theorem.
	 * Keep avoiding this as an optimization.  See log2.c for more
	 * details (some details are here only because the optimization
	 * is not yet available in double precision).
	 *
	 * Another compiler bug turned up.  With gcc on i386,
	 * (ivln2lo + ivln2hi) would be evaluated in float precision
	 * despite runtime evaluations using double precision.  So we
	 * must cast one of its terms to float_t.  This makes the whole
	 * expression have type float_t, so return is forced to waste
	 * time clobbering its extra precision.
	 */
// FIXME
//      if (sizeof(float_t) > sizeof(float))
//              return (r - hfsq + f) * ((float_t)ivln2lo + ivln2hi) + y;

	hi = f - hfsq;
	GET_FLOAT_WORD(hx,hi);
	SET_FLOAT_WORD(hi,hx&0xfffff000);
	lo = (f - hi) - hfsq + r;
	return (lo+hi)*ivln2lo + lo*ivln2hi + hi*ivln2hi + y;
}
