#include <math.h>
#include "libc.h"

int finitel(long double x)
{
	return isfinite(x);
}
