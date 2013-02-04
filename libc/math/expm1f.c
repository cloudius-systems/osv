/* origin: FreeBSD /usr/src/lib/msun/src/s_expm1f.c */
/*
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
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

static const float
huge        = 1.0e+30,
tiny        = 1.0e-30,
o_threshold = 8.8721679688e+01, /* 0x42b17180 */
ln2_hi      = 6.9313812256e-01, /* 0x3f317180 */
ln2_lo      = 9.0580006145e-06, /* 0x3717f7d1 */
invln2      = 1.4426950216e+00, /* 0x3fb8aa3b */
/*
 * Domain [-0.34568, 0.34568], range ~[-6.694e-10, 6.696e-10]:
 * |6 / x * (1 + 2 * (1 / (exp(x) - 1) - 1 / x)) - q(x)| < 2**-30.04
 * Scaled coefficients: Qn_here = 2**n * Qn_for_q (see s_expm1.c):
 */
Q1 = -3.3333212137e-2, /* -0x888868.0p-28 */
Q2 =  1.5807170421e-3; /*  0xcf3010.0p-33 */

float expm1f(float x)
{
	float y,hi,lo,c,t,e,hxs,hfx,r1,twopk;
	int32_t k,xsb;
	uint32_t hx;

	GET_FLOAT_WORD(hx, x);
	xsb = hx&0x80000000;  /* sign bit of x */
	hx &= 0x7fffffff;     /* high word of |x| */

	/* filter out huge and non-finite argument */
	if (hx >= 0x4195b844) {  /* if |x|>=27*ln2 */
		if (hx >= 0x42b17218) {  /* if |x|>=88.721... */
			if (hx > 0x7f800000)  /* NaN */
				return x+x;
			if (hx == 0x7f800000) /* exp(+-inf)={inf,-1} */
				return xsb==0 ? x : -1.0;
			if (x > o_threshold)
				return huge*huge; /* overflow */
		}
		if (xsb != 0) {  /* x < -27*ln2 */
			/* raise inexact */
			if (x+tiny < 0.0f)
				return tiny-1.0f;  /* return -1 */
		}
	}

	/* argument reduction */
	if (hx > 0x3eb17218) {           /* if  |x| > 0.5 ln2 */
		if (hx < 0x3F851592) {       /* and |x| < 1.5 ln2 */
			if (xsb == 0) {
				hi = x - ln2_hi;
				lo = ln2_lo;
				k =  1;
			} else {
				hi = x + ln2_hi;
				lo = -ln2_lo;
				k = -1;
			}
		} else {
			k  = invln2*x + (xsb==0 ? 0.5f : -0.5f);
			t  = k;
			hi = x - t*ln2_hi;      /* t*ln2_hi is exact here */
			lo = t*ln2_lo;
		}
		STRICT_ASSIGN(float, x, hi - lo);
		c = (hi-x)-lo;
	} else if (hx < 0x33000000) {  /* when |x|<2**-25, return x */
		t = huge+x; /* return x with inexact flags when x!=0 */
		return x - (t-(huge+x));
	} else
		k = 0;

	/* x is now in primary range */
	hfx = 0.5f*x;
	hxs = x*hfx;
	r1 = 1.0f+hxs*(Q1+hxs*Q2);
	t  = 3.0f - r1*hfx;
	e  = hxs*((r1-t)/(6.0f - x*t));
	if (k == 0)  /* c is 0 */
		return x - (x*e-hxs);
	SET_FLOAT_WORD(twopk, 0x3f800000+(k<<23));   /* 2^k */
	e  = x*(e-c) - c;
	e -= hxs;
	if (k == -1)
		return 0.5f*(x-e) - 0.5f;
	if (k == 1) {
		if (x < -0.25f)
			return -2.0f*(e-(x+0.5f));
		return 1.0f + 2.0f*(x-e);
	}
	if (k <= -2 || k > 56) {   /* suffice to return exp(x)-1 */
		y = 1.0f - (e - x);
		if (k == 128)
			y = y*2.0f*0x1p127f;
		else
			y = y*twopk;
		return y - 1.0f;
	}
	t = 1.0f;
	if (k < 23) {
		SET_FLOAT_WORD(t, 0x3f800000 - (0x1000000>>k)); /* t=1-2^-k */
		y = t - (e - x);
		y = y*twopk;
	} else {
		SET_FLOAT_WORD(t, (0x7f-k)<<23);  /* 2^-k */
		y = x - (e + t);
		y += 1.0f;
		y = y*twopk;
	}
	return y;
}
