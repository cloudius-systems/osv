#include <ctype.h>

#undef tolower
int tolower(int c)
{
	if (isupper(c)) return c | 32;
	return c;
}
