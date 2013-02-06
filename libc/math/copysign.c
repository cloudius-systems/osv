#include "libm.h"

double copysign(double x, double y) {
	union dshape ux, uy;

	ux.value = x;
	uy.value = y;
	ux.bits &= (uint64_t)-1>>1;
	ux.bits |= uy.bits & (uint64_t)1<<63;
	return ux.value;
}
