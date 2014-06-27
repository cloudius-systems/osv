#include <math.h>
#include "libc.h"

int __finitel(long double x)
{
	return isfinite(x);
}

weak_alias(__finitel,finitel);
