#include <strings.h>
#include "atomic.h"
#include <libc.h>

int ffs(int i)
{
	return i ? a_ctz_l(i)+1 : 0;
}

int ffsl(long i)
{
	return i ? a_ctz_64(i)+1 : 0;
}
weak_alias(ffsl, ffsll);
