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
