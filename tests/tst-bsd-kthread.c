/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#define _KERNEL

#include <osv/debug.h>

#include <bsd/porting/curthread.h>
#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>
#include <bsd/porting/synch.h>
#include <bsd/sys/sys/kthread.h>


struct mutex test_mutex;
static struct thread *test_thread;

static void
thread_loop(void *arg)
{
	mutex_lock(&test_mutex);
	if (curthread != test_thread) {
		kprintf("incorrect curthread or kthread_add return value\n");
		abort();
	}
	mutex_unlock(&test_mutex);

	kthread_exit();
}

int main(int argc, char **argv)
{
	int error;      

	mutex_lock(&test_mutex);
	error = kthread_add(thread_loop, NULL, NULL, &test_thread, 0, 0, "test");
	if (error) {
		kprintf("failed to create kernel thread\n");
		return 1;
	}
	mutex_unlock(&test_mutex);

	/*
	 * Sleep a bit to wait for the thread to terminate.  Unfortunately the
	 * BSD kthread interface has no nice way to wait for thread termination.
	 */
	bsd_pause("kthread_test", 200);
	return 0;
}
