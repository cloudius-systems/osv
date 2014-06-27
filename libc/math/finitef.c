#include <math.h>
#include "libc.h"

int __finitef(float x)
{
	return isfinite(x);
}

weak_alias(__finitef,finitef);
