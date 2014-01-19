#include <stdlib.h>

static unsigned seed;

void srand(unsigned s)
{
	seed = s-1;
}

static inline int _rand_r(unsigned int *seedp)
{
	return (*seedp = (*seedp + 1) * 1103515245 + 12345 - 1) + 1 & 0x7fffffff;
}

int rand_r(unsigned int *seedp)
{
    return _rand_r(seedp);
}

int rand(void)
{
	return _rand_r(&seed);
}
