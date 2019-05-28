#include <string.h>
#include <stdlib.h>

void __explicit_bzero_chk(void *dest, size_t len, size_t destlen)
{
    if (len > destlen) {
        abort();
    }
    memset(dest, 0, len);
    return;
}
