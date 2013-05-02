#include <string.h>
#include <stdlib.h>

void *__memcpy_chk(void *dest, const void *src, size_t len, size_t destlen)
{
    if (len > destlen) {
        abort();
    }
    return memcpy(dest, src, len);
}
