#include <wctype.h>

#undef iswupper_l
int iswupper_l(wint_t c, locale_t l)
{
	return iswupper(c);
}
