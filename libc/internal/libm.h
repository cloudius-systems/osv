/* origin: FreeBSD /usr/src/lib/msun/src/math_private.h */
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

#ifndef _LIBM_H
#define _LIBM_H

#include <stdint.h>
#include <float.h>
#include "math.h"
#include <complex.h>

#include "longdbl.h"

#include "libc.h"

union fshape {
	float value;
	uint32_t bits;
};

union dshape {
	double value;
	uint64_t bits;
};

#define FORCE_EVAL(x) do {                          \
	if (sizeof(x) == sizeof(float)) {           \
		volatile float __x;                 \
		__x = (x);                          \
	} else if (sizeof(x) == sizeof(double)) {   \
		volatile double __x;                \
		__x = (x);                          \
	} else {                                    \
		volatile long double __x;           \
		__x = (x);                          \
	}                                           \
} while(0)

/* Get two 32 bit ints from a double.  */
#define EXTRACT_WORDS(hi,lo,d)                                  \
do {                                                            \
  union dshape __u;                                             \
  __u.value = (d);                                              \
  (hi) = __u.bits >> 32;                                        \
  (lo) = (uint32_t)__u.bits;                                    \
} while (0)

/* Get a 64 bit int from a double.  */
#define EXTRACT_WORD64(i,d)                                     \
do {                                                            \
  union dshape __u;                                             \
  __u.value = (d);                                              \
  (i) = __u.bits;                                               \
} while (0)

/* Get the more significant 32 bit int from a double.  */
#define GET_HIGH_WORD(i,d)                                      \
do {                                                            \
  union dshape __u;                                             \
  __u.value = (d);                                              \
  (i) = __u.bits >> 32;                                         \
} while (0)

/* Get the less significant 32 bit int from a double.  */
#define GET_LOW_WORD(i,d)                                       \
do {                                                            \
  union dshape __u;                                             \
  __u.value = (d);                                              \
  (i) = (uint32_t)__u.bits;                                     \
} while (0)

/* Set a double from two 32 bit ints.  */
#define INSERT_WORDS(d,hi,lo)                                   \
do {                                                            \
  union dshape __u;                                             \
  __u.bits = ((uint64_t)(hi) << 32) | (uint32_t)(lo);           \
  (d) = __u.value;                                              \
} while (0)

/* Set a double from a 64 bit int.  */
#define INSERT_WORD64(d,i)                                      \
do {                                                            \
  union dshape __u;                                             \
  __u.bits = (i);                                               \
  (d) = __u.value;                                              \
} while (0)

/* Set the more significant 32 bits of a double from an int.  */
#define SET_HIGH_WORD(d,hi)                                     \
do {                                                            \
  union dshape __u;                                             \
  __u.value = (d);                                              \
  __u.bits &= 0xffffffff;                                       \
  __u.bits |= (uint64_t)(hi) << 32;                             \
  (d) = __u.value;                                              \
} while (0)

/* Set the less significant 32 bits of a double from an int.  */
#define SET_LOW_WORD(d,lo)                                      \
do {                                                            \
  union dshape __u;                                             \
  __u.value = (d);                                              \
  __u.bits &= 0xffffffff00000000ull;                            \
  __u.bits |= (uint32_t)(lo);                                   \
  (d) = __u.value;                                              \
} while (0)

/* Get a 32 bit int from a float.  */
#define GET_FLOAT_WORD(i,d)                                     \
do {                                                            \
  union fshape __u;                                             \
  __u.value = (d);                                              \
  (i) = __u.bits;                                               \
} while (0)

/* Set a float from a 32 bit int.  */
#define SET_FLOAT_WORD(d,i)                                     \
do {                                                            \
  union fshape __u;                                             \
  __u.bits = (i);                                               \
  (d) = __u.value;                                              \
} while (0)

/* fdlibm kernel functions */

int    __rem_pio2_large(double*,double*,int,int,int);

int    __rem_pio2(double,double*);
double __sin(double,double,int);
double __cos(double,double);
double __tan(double,double,int);
double __expo2(double);
double complex __ldexp_cexp(double complex,int);

int    __rem_pio2f(float,double*);
float  __sindf(double);
float  __cosdf(double);
float  __tandf(double,int);
float  __expo2f(float);
float complex __ldexp_cexpf(float complex,int);

int __rem_pio2l(long double, long double *);
long double __sinl(long double, long double, int);
long double __cosl(long double, long double);
long double __tanl(long double, long double, int);

/* polynomial evaluation */
long double __polevll(long double, const long double *, int);
long double __p1evll(long double, const long double *, int);

#if 0
/* Attempt to get strict C99 semantics for assignment with non-C99 compilers. */
#define STRICT_ASSIGN(type, lval, rval) do {    \
        volatile type __v = (rval);             \
        (lval) = __v;                           \
} while (0)
#else
/* Should work with -fexcess-precision=standard (>=gcc-4.5) or -ffloat-store */
#define STRICT_ASSIGN(type, lval, rval) ((lval) = (type)(rval))
#endif

/* should be in math.h, which we can't include at the moment */
double scalbn(double x, int exp);
int __isnan(double x);


#endif
