#include "stdio_impl.h"

int fputc(int c, FILE *f)
{
	if (f->no_locking || !__lockfile(f))
		return putc_unlocked(c, f);
	c = putc_unlocked(c, f);
	__unlockfile(f);
	return c;
}
