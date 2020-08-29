#include <math.h>
#include "libc.h"

int finitef(float x)
{
	return isfinite(x);
}
