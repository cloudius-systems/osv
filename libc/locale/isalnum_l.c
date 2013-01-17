#include <ctype.h>

#undef isalnum_l
int isalnum_l(int c, locale_t l)
{
	return isalnum(c);
}
