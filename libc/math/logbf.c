#include "libm.h"

float logbf(float x)
{
	if (!isfinite(x))
		return x * x;
	if (x == 0)
		return -1/(x+0);
	return ilogbf(x);
}
