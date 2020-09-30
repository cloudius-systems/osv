#include "stdio_impl.h"

static size_t do_read(FILE *f, unsigned char *buf, size_t len)
{
	return __string_read(f, buf, len);
}

int vsscanf(const char *restrict s, const char *restrict fmt, va_list ap)
{
	FILE f = {
		.buf = (void *)s, .cookie = (void *)s,
		.read = do_read, .no_locking = true,
	};
	return vfscanf(&f, fmt, ap);
}

#if __GNUC__ >= 9
weak_alias(vsscanf,__isoc99_vsscanf) __attribute__((nothrow));
#else
weak_alias(vsscanf,__isoc99_vsscanf);
#endif
