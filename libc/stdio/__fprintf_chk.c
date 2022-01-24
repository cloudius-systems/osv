
#include <stdarg.h>
#include <stdio.h>
#include "libc.h"

int
__fprintf_chk(FILE *fp, int flag, const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = vfprintf(fp, format, ap);
    va_end(ap);

    return ret;
}
