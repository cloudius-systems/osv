#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#undef iswcntrl

int iswblank(wint_t wc)
{
	return isblank(wc);
}
