#include <math.h>
#include "libc.h"

int finite(double x)
{
	return isfinite(x);
}
