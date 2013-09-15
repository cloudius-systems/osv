/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
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

static kcondvar_t	tq_wait;
static kmutex_t		tq_mutex;
static bool		tq_done;

static void
tq_test_func(void *arg)
{
	kprintf("%s called for %s\n", __func__, arg);

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

int main(int argc, char **argv)
{
	if (test_taskq())
		return 1;
	if (test_system_taskq())
		return 1;
	return 0;
}
