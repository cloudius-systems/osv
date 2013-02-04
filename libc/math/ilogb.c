#include <limits.h>
#include "libm.h"

int ilogb(double x)
{
	union dshape u = {x};
	int e = u.bits>>52 & 0x7ff;

	if (!e) {
		u.bits <<= 12;
		if (u.bits == 0) {
			FORCE_EVAL(0/0.0f);
			return FP_ILOGB0;
		}
		/* subnormal x */
		for (e = -0x3ff; u.bits < (uint64_t)1<<63; e--, u.bits<<=1);
		return e;
	}
	if (e == 0x7ff) {
		FORCE_EVAL(0/0.0f);
		return u.bits<<12 ? FP_ILOGBNAN : INT_MAX;
	}
	return e - 0x3ff;
}
