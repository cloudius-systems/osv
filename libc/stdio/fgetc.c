#include "stdio_impl.h"

int fgetc(FILE *f)
{
	int c;
	if (f->no_locking || !__lockfile(f))
		return getc_unlocked(f);
	c = getc_unlocked(f);
	__unlockfile(f);
	return c;
}
