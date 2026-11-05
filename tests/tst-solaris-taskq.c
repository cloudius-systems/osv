/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * Test the solaris taskq interface.
 * As a benefit also exercises the kcondvar and kmutex wrappers.
 */

#include <bsd/porting/netport.h>
#include <sys/taskq.h>
#include <sys/kcondvar.h>
#include <osv/debug.h>
#include <stdio.h>

static kcondvar_t	tq_wait;
static kmutex_t		tq_mutex;
static bool		tq_done;

static void
tq_test_func(void *arg)
{
	kprintf("%s called for %s\n", __func__, (char *) arg);

	mutex_lock(&tq_mutex);
	tq_done = true;
	mutex_unlock(&tq_mutex);

	cv_broadcast(&tq_wait);
}

static int do_test(struct taskq *tq, char *desc)
{
	mutex_lock(&tq_mutex);
	tq_done = false;
	mutex_unlock(&tq_mutex);

	if (taskq_dispatch(tq, tq_test_func, desc, 0) == 0)
		return 1;

	mutex_lock(&tq_mutex);
	while (!tq_done)
		cv_wait(&tq_wait, &tq_mutex);
	mutex_unlock(&tq_mutex);
	return 0;
}

static int test_taskq(void)
{
	struct taskq *tq;

	tq = taskq_create("test_taskq", 1, 0, 0, 0, 0);
	if (!tq) {
		kprintf("failed to create test taskq\n");
		return 1;
	}

	if (do_test(tq, "test taskq"))
		return 1;

	taskq_destroy(tq);
	return 0;
}

static int test_system_taskq(void)
{
	return do_test(system_taskq, "system taskq");
}

/* Exported by libsolaris; the legacy taskq header does not declare them. */
extern void taskq_wait_id(taskq_t *, taskqid_t);
extern int taskq_cancel_id(taskq_t *, taskqid_t, boolean_t);

static void
tq_nop(void *arg)
{
}

#define	NIDS	3000

/*
 * A dispatched task frees itself when it has run.  Its id must still be safe
 * to wait on or cancel afterwards (OpenZFS does both, e.g. zvol_create_minors
 * waits and dmu_objset's upgrade path cancels), and must never name a later
 * task that reuses its memory.
 */
static int test_stale_ids(void)
{
	static taskqid_t ids[NIDS];
	struct taskq *tq;
	int i, j, rc;

	tq = taskq_create("test_taskq_ids", 1, 0, 0, 0, 0);
	if (!tq) {
		printf("failed to create test taskq\n");
		return 1;
	}
	for (i = 0; i < NIDS; i++) {
		ids[i] = taskq_dispatch(tq, tq_nop, NULL, TQ_SLEEP);
		if (ids[i] == 0) {
			printf("FAIL: taskq_dispatch returned 0\n");
			return 1;
		}
		taskq_wait(tq);		/* the task has run and been freed */
		taskq_wait_id(tq, ids[i]);
		rc = taskq_cancel_id(tq, ids[i], B_TRUE);
		if (rc != ENOENT) {
			printf("FAIL: cancel of finished task %d returned %d\n", i, rc);
			return 1;
		}
		for (j = 0; j < i; j++) {
			if (ids[j] == ids[i]) {
				printf("FAIL: task %d reused the id of task %d\n", i, j);
				return 1;
			}
		}
	}
	taskq_destroy(tq);
	printf("stale taskq ids: %d ok\n", NIDS);
	return 0;
}

int main(int argc, char **argv)
{
	if (test_taskq())
		return 1;
	if (test_system_taskq())
		return 1;
	if (test_stale_ids())
		return 1;
	return 0;
}
