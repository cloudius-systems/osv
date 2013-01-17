#include <ctype.h>

#undef islower_l
int islower_l(int c, locale_t l)
{
	return islower(c);
}
