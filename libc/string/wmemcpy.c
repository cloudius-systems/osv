#include <string.h>
#include <wchar.h>

wchar_t *wmemcpy(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n)
{
	wchar_t *a = d;
	while (n--) *d++ = *s++;
	return a;
}
