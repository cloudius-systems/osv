#include <string.h>
#include <assert.h>

/* Used by code compiled on Linux with -D_FORTIFY_SOURCE */
char *__strncpy_chk (char *__restrict d, const char *__restrict s,
    size_t n, size_t len)
{
    assert(len >= n);
    return strncpy(d, s, n);
}
