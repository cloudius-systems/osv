#include "libm.h"

float modff(float x, float *iptr)
{
	union {float x; uint32_t n;} u = {x};
	uint32_t mask;
	int e;

	e = (int)(u.n>>23 & 0xff) - 0x7f;

	/* no fractional part */
	if (e >= 23) {
		*iptr = x;
		if (e == 0x80 && u.n<<9 != 0) { /* nan */
			return x;
		}
		u.n &= 0x80000000;
		return u.x;
	}
	/* no integral part */
	if (e < 0) {
		u.n &= 0x80000000;
		*iptr = u.x;
		return x;
	}

	mask = 0x007fffff>>e;
	if ((u.n & mask) == 0) {
		*iptr = x;
		u.n &= 0x80000000;
		return u.x;
	}
	u.n &= ~mask;
	*iptr = u.x;
	STRICT_ASSIGN(float, x, x - *iptr);
	return x;
}
