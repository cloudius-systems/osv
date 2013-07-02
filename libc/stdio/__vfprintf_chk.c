#include "stdio_impl.h"
#include <stdlib.h>

int __vfprintf_chk(FILE * f, int flag, const char *fmt, va_list ap)
{
    return vfprintf(f, fmt, ap);
}

int __vsnprintf_chk(char *s, size_t maxlen, int flags, size_t slen,
        const char *format, va_list args)
{
    if (slen < maxlen) {
        abort();
    }
    return vsnprintf(s, slen, format, args);
}
