#include <wctype.h>

#undef iswlower_l
int iswlower_l(wint_t c, locale_t l)
{
	return iswlower(c);
}
