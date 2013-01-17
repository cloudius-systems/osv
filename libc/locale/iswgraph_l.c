#include <wctype.h>

#undef iswgraph_l
int iswgraph_l(wint_t c, locale_t l)
{
	return iswgraph(c);
}
