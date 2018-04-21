#include "libc.h"

int res_init()
{
	return 0;
}
weak_alias(res_init, __res_init);
