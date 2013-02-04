#include "libm.h"

int __fpclassify(double x)
{
	union dshape u = { x };
	int e = u.bits>>52 & 0x7ff;
	if (!e) return u.bits<<1 ? FP_SUBNORMAL : FP_ZERO;
	if (e==0x7ff) return u.bits<<12 ? FP_NAN : FP_INFINITE;
	return FP_NORMAL;
}
