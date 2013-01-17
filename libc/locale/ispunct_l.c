#include <ctype.h>

#undef ispunct_l
int ispunct_l(int c, locale_t l)
{
	return ispunct(c);
}
