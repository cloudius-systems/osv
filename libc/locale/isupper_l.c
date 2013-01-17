#include <ctype.h>

#undef isupper_l
int isupper_l(int c, locale_t l)
{
	return isupper(c);
}
