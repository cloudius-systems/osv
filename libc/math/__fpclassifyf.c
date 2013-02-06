#include "libm.h"

int __fpclassifyf(float x)
{
	union fshape u = { x };
	int e = u.bits>>23 & 0xff;
	if (!e) return u.bits<<1 ? FP_SUBNORMAL : FP_ZERO;
	if (e==0xff) return u.bits<<9 ? FP_NAN : FP_INFINITE;
	return FP_NORMAL;
}
