#include <wchar.h>

wchar_t *wcscpy(wchar_t *__restrict d, const wchar_t *__restrict s)
{
	wchar_t *a = d;
	while ((*d++ = *s++));
	return a;
}
