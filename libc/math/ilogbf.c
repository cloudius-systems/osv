#include <limits.h>
#include "libm.h"

int ilogbf(float x)
{
	union fshape u = {x};
	int e = u.bits>>23 & 0xff;

	if (!e) {
		u.bits <<= 9;
		if (u.bits == 0) {
			FORCE_EVAL(0/0.0f);
			return FP_ILOGB0;
		}
		/* subnormal x */
		for (e = -0x7f; u.bits < (uint32_t)1<<31; e--, u.bits<<=1);
		return e;
	}
	if (e == 0xff) {
		FORCE_EVAL(0/0.0f);
		return u.bits<<9 ? FP_ILOGBNAN : INT_MAX;
	}
	return e - 0x7f;
}
