#include <wctype.h>

#undef iswblank_l
int iswblank_l(wint_t c, locale_t l)
{
	return iswblank(c);
}
