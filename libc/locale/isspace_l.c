#include <ctype.h>

#undef isspace_l
int isspace_l(int c, locale_t l)
{
	return isspace(c);
}
