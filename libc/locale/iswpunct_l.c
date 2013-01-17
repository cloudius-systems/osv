#include <wctype.h>

#undef iswpunct_l
int iswpunct_l(wint_t c, locale_t l)
{
	return iswpunct(c);
}
