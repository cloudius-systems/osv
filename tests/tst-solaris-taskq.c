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
#include <unistd.h>

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

/* A task that blocks until released, so later tasks stay queued behind it. */
static kmutex_t		gate_mutex;
static kcondvar_t	gate_cv;
static int		gate_started, gate_open, gate_done;

static void
tq_gate(void *arg)
{
	mutex_lock(&gate_mutex);
	gate_started = 1;
	cv_broadcast(&gate_cv);
	while (!gate_open)
		cv_wait(&gate_cv, &gate_mutex);
	gate_done = 1;
	mutex_unlock(&gate_mutex);
}

static void
gate_reset(void)
{
	mutex_lock(&gate_mutex);
	gate_started = gate_open = gate_done = 0;
	mutex_unlock(&gate_mutex);
}

static void
gate_wait_started(void)
{
	mutex_lock(&gate_mutex);
	while (!gate_started)
		cv_wait(&gate_cv, &gate_mutex);
	mutex_unlock(&gate_mutex);
}

static void
gate_release(void)
{
	mutex_lock(&gate_mutex);
	gate_open = 1;
	cv_broadcast(&gate_cv);
	mutex_unlock(&gate_mutex);
}

static volatile int slow_started, slow_done, mark_ran;

static void
tq_slow(void *arg)
{
	slow_started = 1;
	usleep(200 * 1000);
	slow_done = 1;
}

static void
tq_mark(void *arg)
{
	mark_ran = 1;
}

/*
 * Cancel a task that is still queued: it must report 0, never run, and its
 * id must be gone afterwards.  Cancelling the running gate without waiting
 * must report EBUSY and leave the gate's own free to taskq_run().
 */
static int test_cancel_queued(void)
{
	struct taskq *tq;
	taskqid_t gid, xid;
	int rc, bad = 0;

	tq = taskq_create("test_taskq_cq", 1, 0, 0, 0, 0);
	if (!tq) {
		printf("failed to create test taskq\n");
		return 1;
	}
	gate_reset();
	mark_ran = 0;
	gid = taskq_dispatch(tq, tq_gate, NULL, TQ_SLEEP);
	if (gid == 0) {
		printf("FAIL: taskq_dispatch returned 0\n");
		return 1;
	}
	gate_wait_started();
	xid = taskq_dispatch(tq, tq_mark, NULL, TQ_SLEEP);
	if (xid == 0) {
		printf("FAIL: taskq_dispatch returned 0\n");
		bad = 1;
	} else if ((rc = taskq_cancel_id(tq, xid, B_TRUE)) != 0) {
		printf("FAIL: cancel of queued task returned %d\n", rc);
		bad = 1;
	}
	rc = taskq_cancel_id(tq, gid, B_FALSE);
	if (rc != EBUSY) {
		printf("FAIL: cancel of running task without wait returned %d\n", rc);
		bad = 1;
	}
	gate_release();
	taskq_wait(tq);
	if (mark_ran) {
		printf("FAIL: cancelled task ran\n");
		bad = 1;
	}
	if (!gate_done) {
		printf("FAIL: gate task did not finish\n");
		bad = 1;
	}
	if (xid != 0 && (rc = taskq_cancel_id(tq, xid, B_TRUE)) != ENOENT) {
		printf("FAIL: second cancel of cancelled task returned %d\n", rc);
		bad = 1;
	}
	taskq_destroy(tq);
	if (!bad)
		printf("cancel queued task: ok\n");
	return bad;
}

/*
 * Cancel with wait, and wait_id, on a task that is running: both must return
 * only after the task finished, and the task must be freed exactly once
 * (taskq_run() and the caller each drop a reference).
 */
static int test_cancel_running(void)
{
	struct taskq *tq;
	taskqid_t id;
	int rc, round;

	tq = taskq_create("test_taskq_cr", 1, 0, 0, 0, 0);
	if (!tq) {
		printf("failed to create test taskq\n");
		return 1;
	}
	for (round = 0; round < 2; round++) {
		slow_started = slow_done = 0;
		id = taskq_dispatch(tq, tq_slow, NULL, TQ_SLEEP);
		if (id == 0) {
			printf("FAIL: taskq_dispatch returned 0\n");
			return 1;
		}
		while (!slow_started)
			usleep(1000);
		if (round == 0) {
			rc = taskq_cancel_id(tq, id, B_TRUE);
			if (rc != ENOENT) {
				printf("FAIL: cancel+wait of running task returned %d\n", rc);
				return 1;
			}
		} else {
			taskq_wait_id(tq, id);
		}
		if (!slow_done) {
			printf("FAIL: %s returned before the running task finished\n",
			    round == 0 ? "taskq_cancel_id" : "taskq_wait_id");
			return 1;
		}
		taskq_wait(tq);
		rc = taskq_cancel_id(tq, id, B_TRUE);
		if (rc != ENOENT) {
			printf("FAIL: cancel of finished task returned %d\n", rc);
			return 1;
		}
	}
	taskq_destroy(tq);
	printf("cancel/wait running task: ok\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (test_taskq())
		return 1;
	if (test_system_taskq())
		return 1;
	/* Run every id case, so a failure in one does not hide the others. */
	int fails = test_stale_ids();
	fails += test_cancel_queued();
	fails += test_cancel_running();
	printf("taskq id cases failed: %d\n", fails);
	return fails ? 1 : 0;
}
