#include <wctype.h>

#undef iswalpha_l
int iswalpha_l(wint_t c, locale_t l)
{
	return iswalpha(c);
}
