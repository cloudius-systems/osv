/* origin: FreeBSD /usr/src/lib/msun/src/e_remainderf.c */
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

float remainderf(float x, float p)
{
	int32_t hx,hp;
	uint32_t sx;
	float p_half;

	GET_FLOAT_WORD(hx, x);
	GET_FLOAT_WORD(hp, p);
	sx = hx & 0x80000000;
	hp &= 0x7fffffff;
	hx &= 0x7fffffff;

	/* purge off exception values */
	if (hp == 0 || hx >= 0x7f800000 || hp > 0x7f800000)  /* p = 0, x not finite, p is NaN */
		return (x*p)/(x*p);

	if (hp <= 0x7effffff)
		x = fmodf(x, p + p);  /* now x < 2p */
	if (hx - hp == 0)
		return 0.0f*x;
	x = fabsf(x);
	p = fabsf(p);
	if (hp < 0x01000000) {
		if (x + x > p) {
			x -= p;
			if (x + x >= p)
				x -= p;
		}
	} else {
		p_half = 0.5f*p;
		if (x > p_half) {
			x -= p;
			if (x >= p_half)
				x -= p;
		}
	}
	GET_FLOAT_WORD(hx, x);
	if ((hx & 0x7fffffff) == 0)
		hx = 0;
	SET_FLOAT_WORD(x, hx ^ sx);
	return x;
}
