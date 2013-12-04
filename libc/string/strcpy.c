#include <string.h>

char *__stpcpy(char *, const char *);

char *strcpy(char *__restrict dest, const char *__restrict src)
{
#if 1
	__stpcpy(dest, src);
	return dest;
#else
	const unsigned char *s = src;
	unsigned char *d = dest;
	while ((*d++ = *s++));
	return dest;
#endif
}

/* Used by code compiled on Linux with -D_FORTIFY_SOURCE */
extern char *__stpcpy_chk (char *dest, const char *src, size_t destlen);
char *__strcpy_chk (char *dest, const char *src, size_t destlen)
{
    __stpcpy_chk (dest, src, destlen);
    return dest;
}
