#include "stdio_impl.h"
#include <pthread.h>
#include <limits.h>

int ftrylockfile(FILE *f)
{
	pthread_t self = pthread_self();

	if (f->lock_owner == self) {
		if (f->lockcount == LONG_MAX)
			return -1;
		f->lockcount++;
		return 0;
	}

	if (!mutex_trylock(&f->mutex))
		return -1;
	f->lock_owner = self;
	f->lockcount = 1;
	return 0;
}
