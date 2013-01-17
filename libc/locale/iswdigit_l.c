#include <wctype.h>

#undef iswdigit_l
int iswdigit_l(wint_t c, locale_t l)
{
	return iswdigit(c);
}
