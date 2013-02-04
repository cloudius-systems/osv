#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double copysignl(long double x, long double y)
{
	return copysign(x, y);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
long double copysignl(long double x, long double y)
{
	union ldshape ux = {x}, uy = {y};

	ux.bits.sign = uy.bits.sign;
	return ux.value;
}
#endif
