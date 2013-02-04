/* origin: FreeBSD /usr/src/lib/msun/src/s_log1pf.c */
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
ln2_hi = 6.9313812256e-01, /* 0x3f317180 */
ln2_lo = 9.0580006145e-06, /* 0x3717f7d1 */
two25  = 3.355443200e+07,  /* 0x4c000000 */
Lp1 = 6.6666668653e-01, /* 3F2AAAAB */
Lp2 = 4.0000000596e-01, /* 3ECCCCCD */
Lp3 = 2.8571429849e-01, /* 3E924925 */
Lp4 = 2.2222198546e-01, /* 3E638E29 */
Lp5 = 1.8183572590e-01, /* 3E3A3325 */
Lp6 = 1.5313838422e-01, /* 3E1CD04F */
Lp7 = 1.4798198640e-01; /* 3E178897 */

float log1pf(float x)
{
	float hfsq,f,c,s,z,R,u;
	int32_t k,hx,hu,ax;

	GET_FLOAT_WORD(hx, x);
	ax = hx & 0x7fffffff;

	k = 1;
	if (hx < 0x3ed413d0) {  /* 1+x < sqrt(2)+  */
		if (ax >= 0x3f800000) {  /* x <= -1.0 */
			if (x == -1.0f)
				return -two25/0.0f; /* log1p(-1)=+inf */
			return (x-x)/(x-x);         /* log1p(x<-1)=NaN */
		}
		if (ax < 0x38000000) {   /* |x| < 2**-15 */
			/* raise inexact */
			if (two25 + x > 0.0f && ax < 0x33800000)  /* |x| < 2**-24 */
				return x;
			return x - x*x*0.5f;
		}
		if (hx > 0 || hx <= (int32_t)0xbe95f619) { /* sqrt(2)/2- <= 1+x < sqrt(2)+ */
			k = 0;
			f = x;
			hu = 1;
		}
	}
	if (hx >= 0x7f800000)
		return x+x;
	if (k != 0) {
		if (hx < 0x5a000000) {
			STRICT_ASSIGN(float, u, 1.0f + x);
			GET_FLOAT_WORD(hu, u);
			k = (hu>>23) - 127;
			/* correction term */
			c = k > 0 ? 1.0f-(u-x) : x-(u-1.0f);
			c /= u;
		} else {
			u = x;
			GET_FLOAT_WORD(hu,u);
			k = (hu>>23) - 127;
			c = 0;
		}
		hu &= 0x007fffff;
		/*
		 * The approximation to sqrt(2) used in thresholds is not
		 * critical.  However, the ones used above must give less
		 * strict bounds than the one here so that the k==0 case is
		 * never reached from here, since here we have committed to
		 * using the correction term but don't use it if k==0.
		 */
		if (hu < 0x3504f4) {  /* u < sqrt(2) */
			SET_FLOAT_WORD(u, hu|0x3f800000);  /* normalize u */
		} else {
			k += 1;
			SET_FLOAT_WORD(u, hu|0x3f000000);  /* normalize u/2 */
			hu = (0x00800000-hu)>>2;
		}
		f = u - 1.0f;
	}
	hfsq = 0.5f * f * f;
	if (hu == 0) {  /* |f| < 2**-20 */
		if (f == 0.0f) {
			if (k == 0)
				return 0.0f;
			c += k*ln2_lo;
			return k*ln2_hi+c;
		}
		R = hfsq*(1.0f - 0.66666666666666666f * f);
		if (k == 0)
			return f - R;
		return k*ln2_hi - ((R-(k*ln2_lo+c))-f);
	}
	s = f/(2.0f + f);
	z = s*s;
	R = z*(Lp1+z*(Lp2+z*(Lp3+z*(Lp4+z*(Lp5+z*(Lp6+z*Lp7))))));
	if (k == 0)
		return f - (hfsq-s*(hfsq+R));
	return k*ln2_hi - ((hfsq-(s*(hfsq+R)+(k*ln2_lo+c)))-f);
}
