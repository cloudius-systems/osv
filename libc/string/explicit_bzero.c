#include <string.h>
#include <stdlib.h>

void explicit_bzero(void *dest, size_t len)
{
    memset(dest, 0, len);
    return;
}
