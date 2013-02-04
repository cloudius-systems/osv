#include "libm.h"

float copysignf(float x, float y) {
	union fshape ux, uy;

	ux.value = x;
	uy.value = y;
	ux.bits &= (uint32_t)-1>>1;
	ux.bits |= uy.bits & (uint32_t)1<<31;
	return ux.value;
}
