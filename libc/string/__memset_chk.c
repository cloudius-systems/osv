#include <string.h>
#include <libc/internal/libc.h>

void *__memset_chk(void *dest, int c, size_t n, size_t destlen)
{
    if (n > destlen) {
        _chk_fail("memset");
    }
    return memset(dest, c, n);
}
