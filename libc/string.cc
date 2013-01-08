#include <string.h>
#include <algorithm>

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

const char* strstr(const char* haystack, const char* needle)
{
    auto e1 = haystack + strlen(haystack);
    auto e2 = needle + strlen(needle);
    auto p = std::search(haystack, e1, needle, e2);
    if (p == e1) {
        return nullptr;
    }
    return p;
}
