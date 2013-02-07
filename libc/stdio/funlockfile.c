#include "stdio_impl.h"

void funlockfile(FILE *f)
{
	if (!--f->lockcount) __unlockfile(f);
}
