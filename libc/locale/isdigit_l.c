#include <ctype.h>

#undef isdigit_l
int isdigit_l(int c, locale_t l)
{
	return isdigit(c);
}
