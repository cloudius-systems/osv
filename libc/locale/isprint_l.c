#include <ctype.h>

#undef isprint_l
int isprint_l(int c, locale_t l)
{
	return isprint(c);
}
