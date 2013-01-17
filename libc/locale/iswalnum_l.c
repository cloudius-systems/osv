#include <wctype.h>

#undef iswalnum_l
int iswalnum_l(wint_t c, locale_t l)
{
	return iswalnum(c);
}
