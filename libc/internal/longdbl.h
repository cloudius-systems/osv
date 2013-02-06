#ifndef _LDHACK_H
#define _LDHACK_H

#include <float.h>
#include <stdint.h>

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
union ldshape {
	long double value;
	struct {
		uint64_t m;
		uint16_t exp:15;
		uint16_t sign:1;
		uint16_t pad;
	} bits;
};
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384
union ldshape {
	long double value;
	struct {
		uint64_t mlo;
		uint64_t mhi:48;
		uint16_t exp:15;
		uint16_t sign:1;
	} bits;
};
#else
#error Unsupported long double representation
#endif


// FIXME: hacks to make freebsd+openbsd long double code happy

// union and macros for freebsd

#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384

union IEEEl2bits {
	long double e;
	struct {
		uint32_t manl:32;
		uint32_t manh:32;
		uint32_t exp:15;
		uint32_t sign:1;
		uint32_t pad:16;
	} bits;
	struct {
		uint64_t man:64;
		uint32_t expsign:16;
		uint32_t pad:16;
	} xbits;
};

#define LDBL_MANL_SIZE 32
#define LDBL_MANH_SIZE 32
#define LDBL_NBIT (1ull << LDBL_MANH_SIZE-1)
#undef LDBL_IMPLICIT_NBIT
#define mask_nbit_l(u) ((u).bits.manh &= ~LDBL_NBIT)

#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384
/*
// ld128 float.h
//#define LDBL_MAX 1.189731495357231765085759326628007016E+4932L
#define LDBL_MAX 0x1.ffffffffffffffffffffffffffffp+16383
#define LDBL_MAX_EXP 16384
#define LDBL_HAS_INFINITY 1
//#define LDBL_MIN 3.362103143112093506262677817321752603E-4932L
#define LDBL_MIN 0x1p-16382
#define LDBL_HAS_QUIET_NAN 1
#define LDBL_HAS_DENORM 1
//#define LDBL_EPSILON 1.925929944387235853055977942584927319E-34L
#define LDBL_EPSILON 0x1p-112
#define LDBL_MANT_DIG 113
#define LDBL_MIN_EXP (-16381)
#define LDBL_MAX_10_EXP 4932
#define LDBL_DENORM_MIN 0x0.0000000000000000000000000001p-16381
#define LDBL_MIN_10_EXP (-4931)
#define LDBL_DIG 33
*/

union IEEEl2bits {
	long double e;
	struct {
		uint64_t manl:64;
		uint64_t manh:48;
		uint32_t exp:15;
		uint32_t sign:1;
	} bits;
	struct {
		uint64_t unused0:64;
		uint64_t unused1:48;
		uint32_t expsign:16;
	} xbits;
};

#define LDBL_MANL_SIZE 64
#define LDBL_MANH_SIZE 48
#define LDBL_NBIT (1ull << LDBL_MANH_SIZE)
#define LDBL_IMPLICIT_NBIT 1
#define mask_nbit_l(u)

#endif


// macros for openbsd

#define GET_LDOUBLE_WORDS(se,mh,ml, f) do{ \
	union IEEEl2bits u; \
	u.e = (f); \
	(se) = u.xbits.expsign; \
	(mh) = u.bits.manh; \
	(ml) = u.bits.manl; \
}while(0)

#define SET_LDOUBLE_WORDS(f,  se,mh,ml) do{ \
	union IEEEl2bits u; \
	u.xbits.expsign = (se); \
	u.bits.manh = (mh); \
	u.bits.manl = (ml); \
	(f) = u.e; \
}while(0)

#define GET_LDOUBLE_EXP(se, f) do{ \
	union IEEEl2bits u; \
	u.e = (f); \
	(se) = u.xbits.expsign; \
}while(0)

#define SET_LDOUBLE_EXP(f, se) do{ \
	union IEEEl2bits u; \
	u.e = (f); \
	u.xbits.expsign = (se); \
	(f) = u.e; \
}while(0)

#endif
