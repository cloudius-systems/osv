/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <sys/time.h>
#include <osv/mempool.hh>

#include <bsd/porting/callout.h>

#include <bsd/sys/sys/libkern.h>
#include <bsd/sys/sys/eventhandler.h>

extern "C" {
    // taskqueue
    #include <bsd/sys/sys/taskqueue.h>
    #include <bsd/sys/sys/priority.h>
    TASKQUEUE_DEFINE_THREAD(thread);
}

static void physmem_init()
{
    physmem = memory::phys_mem_size / memory::page_size;
}

void bsd_init(void)
{
    debug("bsd: initializing");

    physmem_init();

    // main taskqueue
    taskqueue_define_thread(NULL);

    // Initialize callouts
    init_callouts();

    /* Random */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    bsd_srandom(tv.tv_sec ^ tv.tv_usec);

    arc4_init();
    eventhandler_init(NULL);

    debug(" - done\n");
}
