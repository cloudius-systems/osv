#include "stdio_impl.h"
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "libc.h"

/* LEAK #2 stdio read-path fix: a FILE struct (and its inline buffer) goes on
 * the shared stdio open-file list (ofl_head) and is dereferenced cross-AS by
 * fflush(NULL) during a sibling backend's flush.  If it were in a fork COW
 * arena window (mapped in one AS only), that deref faults in a reaped/other AS.
 * Scope the FILE+buffer allocation onto the identity heap (shared verbatim in
 * every AS) so the cross-AS deref is always coherent.  Covers fopen (-> this)
 * too.  See core/fork_arena.cc.  No-ops in a non-fork build. */
extern void fork_kernel_heap_push(void);
extern void fork_kernel_heap_pop(void);

FILE *__fdopen(int fd, const char *mode)
{
	FILE *f;
	struct winsize wsz;

	/* Check for valid initial mode character */
	if (!strchr("rwa", *mode)) {
		errno = EINVAL;
		return 0;
	}

	/* Allocate FILE+buffer or fail -- on the identity heap (see above) */
	fork_kernel_heap_push();
	f = malloc(sizeof *f + UNGET + BUFSIZ);
	fork_kernel_heap_pop();
	if (!f) return 0;

	/* Zero-fill only the struct, not the buffer */
	memset(f, 0, sizeof *f);

	/* Impose mode restrictions */
	if (!strchr(mode, '+')) f->flags = (*mode == 'r') ? F_NOWR : F_NORD;

	/* Apply close-on-exec flag */
	if (strchr(mode, 'e')) __syscall(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC);

	/* Set append mode on fd if opened for append */
	if (*mode == 'a') {
		int flags = __syscall(SYS_fcntl, fd, F_GETFL);
		if (!(flags & O_APPEND))
			__syscall(SYS_fcntl, fd, F_SETFL, flags | O_APPEND);
		f->flags |= F_APP;
	}

	f->fd = fd;
	f->buf = (unsigned char *)f + sizeof *f + UNGET;
	f->buf_size = BUFSIZ;

	/* Activate line buffered mode for terminals */
	f->lbf = EOF;
	if (!(f->flags & F_NOWR) && !__syscall(SYS_ioctl, fd, TIOCGWINSZ, &wsz))
		f->lbf = '\n';

	/* Initialize op ptrs. No problem if some are unneeded. */
	f->read = __stdio_read;
	f->write = __stdio_write;
	f->seek = __stdio_seek;
	f->close = __stdio_close;

	/* Add new FILE to open file list */
	return __ofl_add(f);
}

weak_alias(__fdopen, fdopen);
