#include <limits.h>
#include <pthread.h>
#include "stdio_impl.h"

void flockfile(FILE *f)
{
	pthread_t self = pthread_self();

	if (f->lock_owner == self) {
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
	f->lock_owner = self;
	f->lockcount = 1;
}
