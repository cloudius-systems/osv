#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024

#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
int __fpclassifyl(long double x)
{
	union ldshape u = { x };
	int e = u.bits.exp;
	if (!e) {
		if (u.bits.m >> 63) return FP_NAN;
		else if (u.bits.m) return FP_SUBNORMAL;
		else return FP_ZERO;
	}
	if (e == 0x7fff)
		return u.bits.m & (uint64_t)-1>>1 ? FP_NAN : FP_INFINITE;
	return u.bits.m & (uint64_t)1<<63 ? FP_NORMAL : FP_NAN;
}
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384
int __fpclassifyl(long double x)
{
	union ldshape u = { x };
	int e = u.bits.exp;
	if (!e)
		return u.bits.mlo | u.bits.mhi ? FP_SUBNORMAL : FP_ZERO;
	if (e == 0x7fff)
		return u.bits.mlo | u.bits.mhi ? FP_NAN : FP_INFINITE;
	return FP_NORMAL;
}
#endif
