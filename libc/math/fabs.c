#include "libm.h"

double fabs(double x)
{
	union dshape u;

	u.value = x;
	u.bits &= (uint64_t)-1 / 2;
	return u.value;
}
