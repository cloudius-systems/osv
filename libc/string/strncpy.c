#include <string.h>

char *__stpncpy(char *, const char *, size_t);

char *strncpy(char *__restrict d, const char *__restrict s, size_t n)
{
	__stpncpy(d, s, n);
	return d;
}
