#include <string.h>
#include <libc/internal/libc.h>

void * __memmove_chk(void * dest, const void * src, size_t len, size_t destlen)
{
    if (len > destlen) {
        _chk_fail("memmove");
    }
    return memmove(dest, src, len);
}
