#include "libm.h"

double modf(double x, double *iptr)
{
	union {double x; uint64_t n;} u = {x};
	uint64_t mask;
	int e;

	e = (int)(u.n>>52 & 0x7ff) - 0x3ff;

	/* no fractional part */
	if (e >= 52) {
		*iptr = x;
		if (e == 0x400 && u.n<<12 != 0) /* nan */
			return x;
		u.n &= (uint64_t)1<<63;
		return u.x;
	}

	/* no integral part*/
	if (e < 0) {
		u.n &= (uint64_t)1<<63;
		*iptr = u.x;
		return x;
	}

	mask = (uint64_t)-1>>12 >> e;
	if ((u.n & mask) == 0) {
		*iptr = x;
		u.n &= (uint64_t)1<<63;
		return u.x;
	}
	u.n &= ~mask;
	*iptr = u.x;
	STRICT_ASSIGN(double, x, x - *iptr);
	return x;
}
