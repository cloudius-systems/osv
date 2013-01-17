#include <wctype.h>

#undef iswcntrl_l
int iswcntrl_l(wint_t c, locale_t l)
{
	return iswcntrl(c);
}
