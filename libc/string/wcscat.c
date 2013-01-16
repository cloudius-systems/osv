#include <wchar.h>

wchar_t *wcscat(wchar_t *__restrict dest, const wchar_t *__restrict src)
{
	wcscpy(dest + wcslen(dest), src);
	return dest;
}
