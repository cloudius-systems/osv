#include <wctype.h>
#undef iswlower

int iswlower(wint_t wc)
{
	return towupper(wc) != wc || wc == 0xdf;
}
