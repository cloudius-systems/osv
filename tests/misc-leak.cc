/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include <osv/debug.hh>
#include <sys/mman.h>
#include <pthread.h>

// Run this test with "--leak" to check for various previously-known
// memory leaks.

//namespace memory {
//extern bool tracker_enabled;
//}

static void *donothing(void *arg)
{
    debug(".");
    return NULL;
}

int main(int argc, char **argv)
{
//    bool save_tracker_enabled = memory::tracker_enabled;
//    memory::tracker_enabled = true;

    debug("testing leaks in malloc\n");
    for (int i = 0; i < 100; i++) {
        memory::free_page(memory::alloc_page());
    }

    int sizes[] = {4, 888, 4336, 65536};
    for (int size : sizes) {
        for(int i=0; i<100; i++) {
            free(malloc(size));
        }
    }

    debug("testing leaks in mmap\n");
    int mmap_sizes[] = {4096, 4096, 4096*512 };
    for (int size : mmap_sizes) {
        for (int i=0; i < 100; i++) {
            void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
            assert(buf);
            munmap(buf, size);
        }
    }

    debug("testing leaks in threads\n");
    for(int i=0; i<100; i++){
        sched::thread *t = new sched::thread([] {});
        delete t;
    }
    for(int i=0; i<100; i++){
        sched::thread *t = new sched::thread([] {});
        t->start();
        t->join();
        delete t;
    }

    debug("testing leaks in pthread_create");
    for(int i=0; i<10; i++){
        pthread_t t;
        assert(pthread_create(&t, NULL, donothing, NULL) == 0);
        assert(pthread_join(t, NULL) == 0);
    }
    debug("\n");

    debug("testing leaks in PTHREAD_CREATE_DETACHED");
    for(int i=0; i<10; i++){
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_t t;
        assert(pthread_create(&t, &attr, donothing, NULL) == 0);
        pthread_attr_destroy(&attr);
    }
    // sleep enough time for the above threads to complete (TODO: use
    // an atomic counter, wait_until and wake to ensure they all complete).
    sleep(1);
    debug("\n");

    debug("testing leaks in pipe()");
    for(int i = 0; i < 100; i++){
        int p[2];
        assert(pipe(p) == 0);
        assert(close(p[0]) == 0);
        assert(close(p[1]) == 0);
    }
    debug("\n");

    // Sleep a bit, to see if we leak anything in some sort of periodic
    // background thread
    debug("testing background activity (10 seconds)");
    sleep(10);
    debug("\n");


//    debug("leak testing done. Please use 'osv leak show' in gdb to analyze results\n");
//    memory::tracker_enabled = save_tracker_enabled;
}
