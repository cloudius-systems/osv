#include <string.h>
#include <assert.h>

/* Used by code compiled on Linux with -D_FORTIFY_SOURCE */
char *__strcpy_chk (char *dest, const char *src, size_t destlen)
{
    // TODO: This repeats some of strcpy's work. Make it more efficent.
    assert(strlen(src) < destlen);
    return strcpy(dest, src);
}
