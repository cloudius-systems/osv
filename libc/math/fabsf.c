#include "libm.h"

float fabsf(float x)
{
	union fshape u;

	u.value = x;
	u.bits &= (uint32_t)-1 / 2;
	return u.value;
}
