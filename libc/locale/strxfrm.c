#include <string.h>

/* collate only by code points */
size_t strxfrm(char *restrict dest, const char *restrict src, size_t n)
{
	size_t l = strlen(src);
	if (n > l) strcpy(dest, src);
	return l;
}
