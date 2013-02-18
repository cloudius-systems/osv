/*
 * A printf variant that prints directly to the kernel console.
 */

#include "../libc/stdio/stdio_impl.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/uio.h>
#include "debug.h"

static size_t wrap_write(FILE *f, const unsigned char *buf, size_t len)
{
	if (f->wpos-f->wbase)
		debug_write((const char *)f->wbase, f->wpos-f->wbase);

	debug_write((const char *)buf, len);

	f->wend = f->buf + f->buf_size;
	f->wpos = f->wbase = f->buf;
	return len;
}

int vkprintf(const char *restrict fmt, va_list ap)
{
	FILE f = {
		.fd = 1,
		.lbf = EOF,
		.write = wrap_write,
		.buf = (void *)fmt,
		.buf_size = 0,
		.lock_owner = STDIO_SINGLETHREADED,
	};
	return vfprintf(&f, fmt, ap);
}

int kprintf(const char *restrict fmt, ...)
{
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vkprintf(fmt, ap);
	va_end(ap);
	return ret;
}
