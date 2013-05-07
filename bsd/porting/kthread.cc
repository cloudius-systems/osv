
#include <errno.h>
#include <stddef.h>
#include <assert.h>
#include <pthread.h>

#include <bsd/porting/netport.h>
#include <bsd/sys/sys/kthread.h>

int
kthread_add(void (*func)(void *), void *arg, struct proc *p,
		struct thread **newtdp, int flags, int pages,
		const char *fmt, ...)
{
	struct thread *td;
	int error;

	assert(p == NULL);
	assert(flags == RFSTOPPED);
	assert(pages == NULL);

	td = (struct thread *)malloc(sizeof(*td));
	if (!td)
		return ENOMEM;

	error = pthread_create(&td->pthread, NULL, (void *(*) (void *))func, arg);
	if (error)
		goto out_free_td;

	*newtdp = td;
	return 0;

out_free_td:
	free(td);
	return error;
}
