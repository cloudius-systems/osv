#include <assert.h>
#include <pthread.h>
#include "stdio_impl.h"

int __lockfile(FILE *f)
{
	pthread_t self = pthread_self();
	if (f->lock_owner == self)
		return 0;
	mutex_lock(&f->mutex);
	f->lock_owner = self;
	return 1;
}

void __unlockfile(FILE *f)
{
	assert(f->lock_owner == pthread_self());
	f->lock_owner = 0;
	mutex_unlock(&f->mutex);
}
