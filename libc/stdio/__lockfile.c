#include <assert.h>
#include <pthread.h>
#include "stdio_impl.h"

int __lockfile(FILE *f)
{
	if (mutex_owned(&f->mutex))
		return 0;

	mutex_lock(&f->mutex);
	return 1;
}

void __unlockfile(FILE *f)
{
	assert(mutex_owned(&f->mutex));
	mutex_unlock(&f->mutex);
}
