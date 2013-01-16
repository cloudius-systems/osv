#include <wchar.h>

wchar_t *wcpncpy(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n)
{
	return wcsncpy(d, s, n) + wcsnlen(s, n);
}
