#include <ctype.h>

#undef isblank_l
int isblank_l(int c, locale_t l)
{
	return isblank(c);
}
