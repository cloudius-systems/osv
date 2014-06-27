#include <math.h>
#include "libc.h"

int __finite(double x)
{
	return isfinite(x);
}

weak_alias(__finite,finite);
