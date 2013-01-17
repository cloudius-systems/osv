#include <wctype.h>

#undef iswxdigit_l
int iswxdigit_l(wint_t c, locale_t l)
{
	return iswxdigit(c);
}
