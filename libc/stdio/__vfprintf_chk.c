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

int __sprintf_chk(char *s, int flags, size_t slen, const char *format, ...)
{
    va_list args;
    int ret;
    va_start(args, format);
    ret = vsnprintf(s, slen, format, args);
    if (ret > slen) {
        abort();
    }
    va_end(args);
    return ret;
}
