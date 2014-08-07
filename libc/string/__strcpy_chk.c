#include <string.h>

/* Used by code compiled on Linux with -D_FORTIFY_SOURCE */
extern char *__stpcpy_chk (char *dest, const char *src, size_t destlen);
char *__strcpy_chk (char *dest, const char *src, size_t destlen)
{
    __stpcpy_chk (dest, src, destlen);
    return dest;
}
