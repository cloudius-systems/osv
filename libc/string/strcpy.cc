#include <string.h>

char* strcpy(char* dest, const char* src)
{
    char* p = dest;
    while (*p) {
        ++p;
    }
    while (*src) {
        *p++ = *src++;
    }
    *p = '\0';
    return dest;
}
