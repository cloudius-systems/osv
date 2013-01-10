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

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s2 && tolower(*s1) == tolower(*s2)) {
        ++s1, ++s2;
    }
    if (!n || (*s1 == *s2)) {
        return 0;
    }
    return tolower(*s1) < tolower(*s2) ? -1 : 1;
}

char* strdup(const char *s)
{
    auto p = static_cast<char*>(malloc(strlen(s)+1));
    return strcpy(p, s);
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s2 && tolower(*s1) == tolower(*s2)) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) {
        return 0;
    }
    return int(tolower(*s2)) - int(tolower(*s1));
}
