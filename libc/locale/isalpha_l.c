#include <ctype.h>

#undef isalpha_l
int isalpha_l(int c, locale_t l)
{
	return isalpha(c);
}
