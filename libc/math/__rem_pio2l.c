/* origin: FreeBSD /usr/src/lib/msun/ld80/e_rem_pio2.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2008 Steven G. Kargl, David Schultz, Bruce D. Evans.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * Optimized by Bruce D. Evans.
 */
#include "libm.h"
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
/* ld80 version of __rem_pio2(x,y)
 *
 * return the remainder of x rem pi/2 in y[0]+y[1]
 * use __rem_pio2_large() for large x
 */

#define BIAS    (LDBL_MAX_EXP - 1)

/*
 * invpio2:  64 bits of 2/pi
 * pio2_1:   first  39 bits of pi/2
 * pio2_1t:  pi/2 - pio2_1
 * pio2_2:   second 39 bits of pi/2
 * pio2_2t:  pi/2 - (pio2_1+pio2_2)
 * pio2_3:   third  39 bits of pi/2
 * pio2_3t:  pi/2 - (pio2_1+pio2_2+pio2_3)
 */
static const double
two24  =  1.67772160000000000000e+07, /* 0x41700000, 0x00000000 */
pio2_1 =  1.57079632679597125389e+00, /* 0x3FF921FB, 0x54444000 */
pio2_2 = -1.07463465549783099519e-12, /* -0x12e7b967674000.0p-92 */
pio2_3 =  6.36831716351370313614e-25; /*  0x18a2e037074000.0p-133 */

static const long double
invpio2 =  6.36619772367581343076e-01L, /*  0xa2f9836e4e44152a.0p-64 */
pio2_1t = -1.07463465549719416346e-12L, /* -0x973dcb3b399d747f.0p-103 */
pio2_2t =  6.36831716351095013979e-25L, /*  0xc51701b839a25205.0p-144 */
pio2_3t = -2.75299651904407171810e-37L; /* -0xbb5bf6c7ddd660ce.0p-185 */

int __rem_pio2l(long double x, long double *y)
{
	union IEEEl2bits u,u1;
	long double z,w,t,r,fn;
	double tx[3],ty[2];
	int e0,ex,i,j,nx,n;
	int16_t expsign;

	u.e = x;
	expsign = u.xbits.expsign;
	ex = expsign & 0x7fff;
	if (ex < BIAS + 25 || (ex == BIAS + 25 && u.bits.manh < 0xc90fdaa2)) {
		union IEEEl2bits u2;
		int ex1;

		/* |x| ~< 2^25*(pi/2), medium size */
		/* Use a specialized rint() to get fn.  Assume round-to-nearest. */
		fn = x*invpio2 + 0x1.8p63;
		fn = fn - 0x1.8p63;
// FIXME
//#ifdef HAVE_EFFICIENT_IRINT
//		n = irint(fn);
//#else
		n = fn;
//#endif
		r = x-fn*pio2_1;
		w = fn*pio2_1t;    /* 1st round good to 102 bit */
		j = ex;
		y[0] = r-w;
		u2.e = y[0];
		ex1 = u2.xbits.expsign & 0x7fff;
		i = j-ex1;
		if (i > 22) {  /* 2nd iteration needed, good to 141 */
			t = r;
			w = fn*pio2_2;
			r = t-w;
			w = fn*pio2_2t-((t-r)-w);
			y[0] = r-w;
			u2.e = y[0];
			ex1 = u2.xbits.expsign & 0x7fff;
			i = j-ex1;
			if (i > 61) {  /* 3rd iteration need, 180 bits acc */
				t = r; /* will cover all possible cases */
				w = fn*pio2_3;
				r = t-w;
				w = fn*pio2_3t-((t-r)-w);
				y[0] = r-w;
			}
		}
		y[1] = (r - y[0]) - w;
		return n;
	}
	/*
	 * all other (large) arguments
	 */
	if (ex == 0x7fff) {                /* x is inf or NaN */
		y[0] = y[1] = x - x;
		return 0;
	}
	/* set z = scalbn(|x|,ilogb(x)-23) */
	u1.e = x;
	e0 = ex - BIAS - 23;            /* e0 = ilogb(|x|)-23; */
	u1.xbits.expsign = ex - e0;
	z = u1.e;
	for (i=0; i<2; i++) {
		tx[i] = (double)(int32_t)z;
		z     = (z-tx[i])*two24;
	}
	tx[2] = z;
	nx = 3;
	while (tx[nx-1] == 0.0)
		nx--;     /* skip zero term */
	n = __rem_pio2_large(tx,ty,e0,nx,2);
	r = (long double)ty[0] + ty[1];
	w = ty[1] - (r - ty[0]);
	if (expsign < 0) {
		y[0] = -r;
		y[1] = -w;
		return -n;
	}
	y[0] = r;
	y[1] = w;
	return n;
}
#endif
