#include <ctype.h>

#undef isxdigit_l
int isxdigit_l(int c, locale_t l)
{
	return isxdigit(c);
}
