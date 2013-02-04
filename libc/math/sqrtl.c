#include <math.h>

long double sqrtl(long double x)
{
	/* FIXME: implement sqrtl in C. At least this works for now on
	 * ARM (which uses ld64), the only arch without sqrtl asm
	 * that's supported so far. */
	return sqrt(x);
}
