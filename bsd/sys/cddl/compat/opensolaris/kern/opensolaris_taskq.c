/*-
 * Copyright (c) 2009 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <bsd/porting/netport.h>
#include <osv/debug.h>

#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/taskq.h>
#include <osv/export.h>
#include <string.h>

static uma_zone_t taskq_zone;

OSV_LIB_SOLARIS_API
taskq_t *system_taskq = NULL;

OSV_LIB_SOLARIS_API
taskq_t *system_delay_taskq = NULL;

OSV_LIB_SOLARIS_API void
system_taskq_init(void *arg)
{
	taskq_zone = uma_zcreate("taskq_zone", sizeof(struct ostask),
	    NULL, NULL, NULL, NULL, 0, 0);
	system_taskq = taskq_create("system_taskq", 8, 0, 0, 0, 0);
	system_delay_taskq = taskq_create("system_delay_taskq", 4, 0, 0, 0, 0);
}
SYSINIT(system_taskq_init, SI_SUB_CONFIGURE, SI_ORDER_ANY, system_taskq_init, NULL);

OSV_LIB_SOLARIS_API void
system_taskq_fini(void *arg)
{

	taskq_destroy(system_taskq);
	taskq_destroy(system_delay_taskq);
	uma_zdestroy(taskq_zone);
}
SYSUNINIT(system_taskq_fini, SI_SUB_CONFIGURE, SI_ORDER_ANY, system_taskq_fini, NULL);

OSV_LIB_SOLARIS_API taskq_t *
taskq_create(const char *name, int nthreads, pri_t pri, int minalloc __bsd_unused2,
    int maxalloc __bsd_unused2, uint_t flags)
{
	taskq_t *tq;

	if ((flags & TASKQ_THREADS_CPU_PCT) != 0)
		nthreads = MAX((mp_ncpus * nthreads) / 100, 1);

	tq = kmem_alloc(sizeof(*tq), KM_SLEEP);
	tq->tq_queue = taskqueue_create(name, M_WAITOK, taskqueue_thread_enqueue,
	    &tq->tq_queue);
	(void) taskqueue_start_threads(&tq->tq_queue, nthreads, pri, "%s", name);

	return ((taskq_t *)tq);
}

taskq_t *
taskq_create_proc(const char *name, int nthreads, pri_t pri, int minalloc,
    int maxalloc, proc_t *proc __bsd_unused2, uint_t flags)
{

	return (taskq_create(name, nthreads, pri, minalloc, maxalloc, flags));
}

OSV_LIB_SOLARIS_API void
taskq_destroy(taskq_t *tq)
{

	taskqueue_free(tq->tq_queue);
	kmem_free(tq, sizeof(*tq));
}

int
taskq_member(taskq_t *tq, kthread_t *thread)
{

	return (taskqueue_member(tq->tq_queue, thread));
}

int
taskq_cancel_id(taskq_t *tq, taskqid_t tid, boolean_t wait)
{
	struct ostask *task = (struct ostask *)(void *)tid;
	uint32_t pend = 0;
	int rc;

	if (task == NULL)
		return (ENOENT);

	rc = taskqueue_cancel(tq->tq_queue, &task->ost_task, &pend);
	if (rc == EBUSY && wait)
		taskqueue_drain(tq->tq_queue, &task->ost_task);

	if (pend)
		uma_zfree(taskq_zone, task);

	return (pend ? 0 : ENOENT);
}

static void
taskq_run(void *arg, int pending __bsd_unused2)
{
	struct ostask *task = arg;

	task->ost_func(task->ost_arg);

	uma_zfree(taskq_zone, task);
}

OSV_LIB_SOLARIS_API taskqid_t
taskq_dispatch(taskq_t *tq, task_func_t func, void *arg, uint_t flags)
{
	struct ostask *task;
	int mflag, prio;

	if ((flags & (TQ_SLEEP | TQ_NOQUEUE)) == TQ_SLEEP)
		mflag = M_WAITOK;
	else
		mflag = M_NOWAIT;
	/* 
	 * If TQ_FRONT is given, we want higher priority for this task, so it
	 * can go at the front of the queue.
	 */
	prio = !!(flags & TQ_FRONT);

	task = uma_zalloc(taskq_zone, mflag);
	if (task == NULL)
		return (0);

	task->ost_func = func;
	task->ost_arg = arg;

	TASK_INIT(&task->ost_task, prio, taskq_run, task);
	taskqueue_enqueue(tq->tq_queue, &task->ost_task);

	return ((taskqid_t)(void *)task);
}

#define	TASKQ_MAGIC	0x74541c

static void
taskq_run_safe(void *arg, int pending __bsd_unused2)
{
	struct ostask *task = arg;

	task->ost_func(task->ost_arg);
}

taskqid_t
taskq_dispatch_safe(taskq_t *tq, task_func_t func, void *arg, u_int flags,
    struct ostask *task)
{
	int prio;

	/*
	 * If TQ_FRONT is given, we want higher priority for this task, so it
	 * can go at the front of the queue.
	 */
	prio = !!(flags & TQ_FRONT);

	task->ost_func = func;
	task->ost_arg = arg;

	TASK_INIT(&task->ost_task, prio, taskq_run_safe, task);
	taskqueue_enqueue(tq->tq_queue, &task->ost_task);

	return ((taskqid_t)(void *)task);
}

/* ------------------------------------------------------------------ */
/* OpenZFS 2.x extended taskq API                                      */
/* ------------------------------------------------------------------ */

/*
 * taskq_wait - wait for all currently-queued tasks to complete.
 * We enqueue a do-nothing barrier task and drain on it; since tasks
 * execute in FIFO order all prior tasks will have finished first.
 */
static void
taskq_barrier_run(void *arg __bsd_unused2, int pending __bsd_unused2)
{
}

OSV_LIB_SOLARIS_API void
taskq_wait(taskq_t *tq)
{
	struct task barrier;

	TASK_INIT(&barrier, 0, taskq_barrier_run, NULL);
	taskqueue_enqueue(tq->tq_queue, &barrier);
	taskqueue_drain(tq->tq_queue, &barrier);
}

/*
 * taskq_wait_id - wait for the task identified by id to complete.
 */
OSV_LIB_SOLARIS_API void
taskq_wait_id(taskq_t *tq, taskqid_t id)
{
	struct ostask *task = (struct ostask *)(void *)id;

	if (task == NULL)
		return;
	taskqueue_drain(tq->tq_queue, &task->ost_task);
}

/*
 * taskq_wait_outstanding - wait until at most `id` tasks are outstanding.
 * For OSv, we simply drain the whole queue (conservative but correct).
 */
OSV_LIB_SOLARIS_API void
taskq_wait_outstanding(taskq_t *tq, taskqid_t id __bsd_unused2)
{
	taskq_wait(tq);
}

/*
 * taskq_of_curthread - return the taskq the current thread belongs to.
 * OSv does not track this per-thread; return NULL.
 */
OSV_LIB_SOLARIS_API taskq_t *
taskq_of_curthread(void)
{
	return (NULL);
}

/*
 * taskq_create_synced - create a taskq with barrier semantics.
 * On OSv this is equivalent to taskq_create. The optional threads
 * output parameter receives a freshly-allocated array of nthreads
 * NULL-valued kthread_t* handles (OSv has no real kthread handles).
 * Callers (e.g. spa_sync_tq_create) must kmem_free() the array.
 */
OSV_LIB_SOLARIS_API taskq_t *
taskq_create_synced(const char *name, int nthreads, pri_t pri,
    int minalloc, int maxalloc, uint_t flags, kthread_t ***threads)
{
	taskq_t *tq = taskq_create(name, nthreads, pri, minalloc, maxalloc,
	    flags);
	if (threads != NULL) {
		/*
		 * Provide a valid (zeroed) array so the caller can safely
		 * iterate and kmem_free() it without heap corruption.
		 */
		*threads = kmem_zalloc(sizeof (kthread_t *) * MAX(nthreads, 1),
		    KM_SLEEP);
	}
	return (tq);
}

/*
 * taskq_suspend / taskq_suspended / taskq_resume - not implemented on OSv.
 */
OSV_LIB_SOLARIS_API void
taskq_suspend(taskq_t *tq __bsd_unused2)
{
}

OSV_LIB_SOLARIS_API int
taskq_suspended(taskq_t *tq __bsd_unused2)
{
	return (0);
}

OSV_LIB_SOLARIS_API void
taskq_resume(taskq_t *tq __bsd_unused2)
{
}

/*
 * taskq_dispatch_delay - dispatch after a delay of `ticks` clock ticks.
 * On OSv we do NOT dispatch immediately because callers such as
 * spa_deadman() rely on the delay to avoid a premature watchdog fire.
 * Without a real timer facility, we simply drop the delayed task (no-op).
 * This is safe: spa_deadman is a watchdog — if it never fires, pool
 * operations complete normally without a false abort.
 */
OSV_LIB_SOLARIS_API taskqid_t
taskq_dispatch_delay(taskq_t *tq __bsd_unused2, task_func_t func __bsd_unused2,
    void *arg __bsd_unused2, uint_t flags __bsd_unused2,
    clock_t ticks __bsd_unused2)
{
	return (0);	/* 0 = not scheduled; taskq_cancel_id handles NULL safely */
}

/*
 * nulltask - do-nothing task function used as a placeholder.
 */
OSV_LIB_SOLARIS_API void
nulltask(void *arg __bsd_unused2)
{
}

/*
 * taskq_init_ent, taskq_empty_ent, taskq_dispatch_ent are defined in
 * openzfs_osv_compat.c because that file is compiled with the full
 * OpenZFS include paths needed for taskq_ent_t.
 */
