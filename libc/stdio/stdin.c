#include "stdio_impl.h"

static size_t __stdin_read(FILE *fp, unsigned char *buf, size_t len)
{
    // Flush stdout when reading from stdin - this is a long UNIX tradition
    // and also happens in glibc. The latter flushes stdout if it is line-
    // buffered whenever any line-buffered file is read - but the intended
    // effect was on stdin.
    if (stdout->lbf == '\n') {
        fflush(stdout);
    }
    return __stdio_read(fp, buf, len);
}

static unsigned char buf[BUFSIZ+UNGET];
static FILE f = {
	.buf = buf+UNGET,
	.buf_size = sizeof buf-UNGET,
	.fd = 0,
	.flags = F_PERM | F_NOWR,
	.read = __stdin_read,
	.seek = __stdio_seek,
	.close = __stdio_close,
};
FILE *const stdin = &f;
FILE *const __stdin_used = &f;
