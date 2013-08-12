
#define _KERNEL

#include <osv/debug.h>

#include <bsd/porting/netport.h>
#include <bsd/sys/sys/priority.h>
#include <bsd/sys/sys/taskqueue.h>

static void
task_worker(void *context, int pending)
{
	kprintf("worker called\n");
}

int main(int argc, char **argv)
{
	struct taskqueue *t;
	struct task task;
	int retval;

	t = taskqueue_create("test", M_WAITOK, taskqueue_thread_enqueue, &t);
	if (!t) {
		kprintf("unable to create taskqueue\n");
		return 1;
	}

	retval = taskqueue_start_threads(&t,
					 4, 	/*num threads*/
					 PWAIT,	/*priority*/
					 "%s",	/* thread name */
					 "test");
	if (retval != 0) {
		kprintf("failed to create taskqueue threads\n");
		return 1;
	}

	TASK_INIT(&task, /*priority*/0, task_worker, NULL);

	retval = taskqueue_enqueue(t, &task);
	if (retval != 0) {
		kprintf("failed to enqueue task\n");
		return 1;
	}

	taskqueue_drain(t, &task);

	taskqueue_free(t);
	return 0;
}
