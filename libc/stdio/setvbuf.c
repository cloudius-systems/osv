#include "stdio_impl.h"

/* The behavior of this function is undefined except when it is the first
 * operation on the stream. OSv cannot rely on that for stdout and stderr,
 * see below. */

int setvbuf(FILE *restrict f, char *restrict buf, int type, size_t size)
{
	// In OSv, the kernel and every application share one stdout and one
	// stderr, and the kernel has usually written to them before an
	// application's main() runs, so setvbuf() is rarely the first operation
	// on those streams.  If we only changed buf_size, the write pointers
	// would keep pointing at the old buffer, and later writes would be
	// copied there instead of being written, even with _IONBF.  So flush
	// any pending output and clear the write state, under the stream's
	// lock as fflush() does, so the next write starts over with the new
	// buffering.  On a stream that has not been written to, this does
	// nothing.  The read state is left alone: unread bytes in the old
	// buffer are still returned, and the next refill uses the new buffer.
	FLOCK(f);
	if (f->wpos != f->wbase) f->write(f, 0, 0);
	f->wpos = f->wbase = f->wend = 0;

	f->lbf = EOF;

	if (type == _IONBF) {
		f->buf_size = 0;
	} else if (type == _IOLBF || type == _IOFBF) {
        // Because of https://github.com/cloudius-systems/osv/issues/1180
        // in OSv we need to subtract BSIZE (512) bytes, where Musl subtracts
        // UNGET (8) bytes from the user's buffer size. This subtraction is
        // ugly and wasteful and we should eventually consider a different
        // approach. Two better but considerably more complex approaches:
        // 1. Move the UNGET buffer to be in the FILE object, not the
        //    buffer, so we could use the full buffer for actual reading.
        // 2. Allocate size bytes here (by the way, POSIX does this when
        //    buf==NULL and we don't support it yet) - instead of using
        //    the user's buffer. We can size the allocation at UNGET+size
        //    so we'll have real buffer size of "size".
        //    The problem with this approach is that we'll need to remember
        //    to free this buffer when the file is closed (or buffer is
        //    changed again) and this code is currently missing.
#define BSIZE_ALIGNED_UNGET 512
		if (buf && size >= BSIZE_ALIGNED_UNGET) {
			f->buf = (void *)(buf + BSIZE_ALIGNED_UNGET);
			f->buf_size = size - BSIZE_ALIGNED_UNGET;
		}
		if (type == _IOLBF && f->buf_size)
			f->lbf = '\n';
	} else {
		FUNLOCK(f);
		return -1;
	}

	f->flags |= F_SVB;

	FUNLOCK(f);
	return 0;
}
