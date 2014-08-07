#include <string.h>
#include <assert.h>

/* Used by code compiled on Linux with -D_FORTIFY_SOURCE */
char *__stpcpy_chk (char *dest, const char *src, size_t destlen)
{
    // TODO: This repeats some of stpcpy's work. Make it more efficent.
    assert(strlen(src) < destlen);
    return stpcpy(dest, src);
}
