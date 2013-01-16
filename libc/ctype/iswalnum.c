#include <wchar.h>
#include <wctype.h>
#undef iswalnum

int iswalnum(wint_t wc)
{
	return iswdigit(wc) || iswalpha(wc);
}
