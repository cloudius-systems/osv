#include <string.h>

const char* strrchr(const char* s, int c)
{
    for (const char *p = s + strlen(s); p >= s; --p) {
        if (*p == c) {
            return const_cast<char*>(p);
        }
    }
    return nullptr;
}

const char* strchr(const char* s, int c)
{
    do {
        if (*s == c) {
            return s;
        }
    } while (*s++);
    return nullptr;
}

char* stpcpy(char* p, const char* s)
{
    while (*s) {
        *p++ = *s++;
    }
    *p = '\0';
    return p;
}
