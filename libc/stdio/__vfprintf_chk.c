#include "stdio_impl.h"

int __vfprintf_chk(FILE * f, int flag, const char *fmt, va_list ap)
{
    return vfprintf(f, fmt, ap);
}
