#include <stdio.h>
#include <stdarg.h>
#include "libc.h"

int sscanf(const char *restrict s, const char *restrict fmt, ...)
{
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vsscanf(s, fmt, ap);
	va_end(ap);
	return ret;
}

#if __GNUC__ >= 9
weak_alias(sscanf,__isoc99_sscanf) __attribute__((nothrow));
#else
weak_alias(sscanf,__isoc99_sscanf);
#endif
