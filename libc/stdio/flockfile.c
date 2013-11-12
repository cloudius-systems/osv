#include <limits.h>
#include <pthread.h>
#include "stdio_impl.h"

void flockfile(FILE *f)
{
	if (mutex_owned(&f->mutex)) {
		if (f->lockcount < LONG_MAX) {
			f->lockcount++;
			return;
		}

		/*
		 * Hack: just waits until all references are gone,
		 * instead of waiting for the count to go below
		 * LONG_MAX.
		 */
	}

	mutex_lock(&f->mutex);
	f->lockcount = 1;
}
