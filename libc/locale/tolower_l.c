#include <ctype.h>

#undef tolower_l
int tolower_l(int c, locale_t l)
{
	return tolower(c);
}
