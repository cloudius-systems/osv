#include "stdio_impl.h"
#include <sys/uio.h>
#include <pthread.h>

#include "pthread_stubs.h"

#if 0
static void cleanup(void *p)
{
	FILE *f = p;
	if (!f->lockcount) __unlockfile(f);
}
#endif

size_t __stdio_read(FILE *f, unsigned char *buf, size_t len)
{
	struct iovec iov[2] = {
		{ .iov_base = buf, .iov_len = len },
		{ .iov_base = f->buf, .iov_len = f->buf_size }
	};
	ssize_t cnt;

	if (libc.main_thread) {
		pthread_cleanup_push(cleanup, f);
		cnt = readv(f->fd, iov, 2);
		pthread_cleanup_pop(0);
	} else {
		cnt = readv(f->fd, iov, 2);
	}
	if (cnt <= 0) {
		f->flags |= F_EOF ^ ((F_ERR^F_EOF) & cnt);
		f->rpos = f->rend = 0;
		return cnt;
	}
	if (cnt <= len) return cnt;
	cnt -= len;
	f->rpos = f->buf;
	f->rend = f->buf + cnt;
	return len;
}
