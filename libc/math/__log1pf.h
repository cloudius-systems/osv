/* origin: FreeBSD /usr/src/lib/msun/src/k_logf.h */
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
 * See comments in __log1p.h.
 */

/* |(log(1+s)-log(1-s))/s - Lg(s)| < 2**-34.24 (~[-4.95e-11, 4.97e-11]). */
static const float
Lg1 = 0xaaaaaa.0p-24, /* 0.66666662693 */
Lg2 = 0xccce13.0p-25, /* 0.40000972152 */
Lg3 = 0x91e9ee.0p-25, /* 0.28498786688 */
Lg4 = 0xf89e26.0p-26; /* 0.24279078841 */

static inline float __log1pf(float f)
{
	float hfsq,s,z,R,w,t1,t2;

	s = f/(2.0f + f);
	z = s*s;
	w = z*z;
	t1 = w*(Lg2+w*Lg4);
	t2 = z*(Lg1+w*Lg3);
	R = t2+t1;
	hfsq = 0.5f * f * f;
	return s*(hfsq+R);
}
