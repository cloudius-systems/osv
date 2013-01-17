#include <ctype.h>

#undef isgraph_l
int isgraph_l(int c, locale_t l)
{
	return isgraph(c);
}
