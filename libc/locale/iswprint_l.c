#include <wctype.h>

#undef iswprint_l
int iswprint_l(wint_t c, locale_t l)
{
	return iswprint(c);
}
