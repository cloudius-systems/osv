#include <wchar.h>

wchar_t *wcpcpy(wchar_t *__restrict d, const wchar_t *__restrict s)
{
	return wcscpy(d, s) + wcslen(s);
}
