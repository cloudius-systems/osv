/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <osv/sched.hh>
#include <osv/debug.hh>

#include <osv/condvar.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
    debug("Running wakeup idiom tests\n");
    debug("Test 1 - wake_with\n");
    // Test the wakeup idiom used in condvar, lockfree::mutex, and others.
    // The idiom used to look like:
    //   t = wr->t
    //   wr->t = nullptr;
    //   t->wake();
    // But this had a bug where wr->t = nullptr may wake up the thread
    // which may exit and then t->wake() will crash. We make this case
    // easy to catch with a spurious wakeup and mprotected pages.
    // This test verifies that the improved wakeup idiom works:
    //   wr->t->wake_with([] { wr->t = nullptr; });
    int npages = (sizeof(sched::thread)-1)/4096+1;
    void *pages[4];
    pages[0] = mmap(NULL, npages*4096, PROT_NONE, 0, 0, 0);
    pages[1] = mmap(NULL, npages*4096, PROT_NONE, 0, 0, 0);
    pages[2] = mmap(NULL, npages*4096, PROT_NONE, 0, 0, 0);
    pages[3] = mmap(NULL, npages*4096, PROT_NONE, 0, 0, 0);
    // double-buffering - two page regions out of the above four hold the
    // current two threads, and two are mprotect()ed to catch access to the
    // previously deleted threads.
    int db = 0;

    for (int i=0; i<10000; i++) {
        std::atomic<sched::thread *>thread;
        db = 1-db;
        mprotect(pages[db*2+0], npages*4096, PROT_READ|PROT_WRITE);
        mprotect(pages[db*2+1], npages*4096, PROT_READ|PROT_WRITE);
        auto t1 = new(pages[db*2+0]) sched::thread([&] {
            while(!thread.load())
                ;
            sched::thread *t = thread.load();
            t->wake_with([&] {
                    thread.store(nullptr);
                    t->wake(); // spurious wakeup!
                    for(int j=0; j<100000; j++) asm(""); // some deliberate slowdown
            });
        });
        t1->start();
        auto t2 = new(pages[db*2+1]) sched::thread([&] {
            sched::thread::wait_until([&] { return !thread.load(); });
            // immediate exit this thread after being woken up.
        });
        thread.store(t2);
        t2->start();
        t2->~thread();
        mprotect(pages[db*2+1], npages*4096, PROT_NONE);
        t1->~thread();
        mprotect(pages[db*2+0], npages*4096, PROT_NONE);
    }

    debug("Test 2 - stress test (can take roughly a minute - if it hangs for more - suspect!)\n");
    // The stress test is a very similar test, without all the artificial
    // mprotect, wakeups and waits used to cause the problem to happen
    // quickly. Because the iteration time is faster now, we can run many
    // more iterations here, and it stresses thread creation and joining
    // much more than the previous test.
    // Before commit cf4c46c482f977036118fb6db91d6f7e69f84e12, this test used
    // to hang.
    for (int i=0; i<10000000; i++) {
        std::atomic<sched::thread *>thread;
        auto t1 = new sched::thread([&] {
            while(!thread.load())
                ;
            thread.load()->wake_with([&] { thread.store(nullptr); });
        });
        t1->start();
        auto t2 = new sched::thread([&] {
            sched::thread::wait_until([&] { return !thread.load(); });
        });
        thread.store(t2);
        t2->start();
        delete t2;
        delete t1;
    }

    debug("wakeup idiom succeeded\n");
    return 0;

}
