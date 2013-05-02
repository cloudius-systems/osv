#include <stdlib.h>

static unsigned seed;

void srand(unsigned s)
{
	seed = s-1;
}

int rand(void)
{
	return (seed = (seed+1) * 1103515245 + 12345 - 1)+1 & 0x7fffffff;
}
