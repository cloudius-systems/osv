/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <atomic>

#include "sched.hh"
#include "debug.hh"
#include <drivers/clock.hh>

#include <osv/condvar.h>

void assert_idle(condvar *c)
{
    assert (!c->_waiters_fifo.newest);
    assert (!c->_waiters_fifo.oldest);
}

int main(int argc, char **argv)
{
    debug("Running condition variable tests\n");

    // Test trivial single-thread tests
    debug("test1\n");
    condvar cond = CONDVAR_INITIALIZER;
    assert_idle(&cond);
    // See that wake for condition variable nobody wait on do not cause havoc
    cond.wake_all();
    cond.wake_one();
    assert_idle(&cond);

    // A basic two-thread test - one thread waits for the other
    debug("test2\n");
    mutex m;
    int res=0;
    sched::thread *t1 = new sched::thread([&cond,&m,&res] {
        m.lock();
        while (res==0) {
            cond.wait(&m);
        }
        res = 2;
        m.unlock();
    });
    sched::thread *t2 = new sched::thread([&cond,&m,&res] {
        m.lock();
        res = 1;
        m.unlock();
        cond.wake_one();
    });

    t1->start();
    t2->start();
    t1->join();
    t2->join();
    delete t1;
    delete t2;
    assert_idle(&cond);

    // A test where N threads wait on a single condition
    // variable, and when all are ready (using an atomic counter
    // and a second condition variable) another thread wakes them all
    // with wake_all or wake_one.
    constexpr int N = 50;
    debug("test3, with %d threads\n", N);
    int ready = 0;
    condvar done = CONDVAR_INITIALIZER;
    sched::thread *threads[N];
    for (int i = 0; i < N; i++) {
            threads[i] = new sched::thread([&cond, &m, &ready, &done] {
                m.lock();
                ready++;
                //debug("ready %d\n",ready);
                done.wake_one();
                m.unlock();

                m.lock();
                while(ready < N)
                    cond.wait(&m);
                m.unlock();
                m.lock();
                ready++;
                //debug("woken %d\n",ready);
                m.unlock();
                done.wake_one();

                m.lock();
                while(ready < N*2)
                    cond.wait(&m);
                m.unlock();
                m.lock();
                ready++;
                //debug("woken2 %d\n",ready);
                m.unlock();
                done.wake_one();
            });
    }
    t1 = new sched::thread([&cond, &m, &ready, &done] {
        m.lock();
        while (ready < N) {
            done.wait(&m);
        }
        m.unlock();
        debug("waking all\n");
        m.lock();
        assert (ready >= N);
        cond.wake_all();
        m.unlock();
        m.lock();
        while (ready < N*2) {
            done.wait(&m);
        }
        m.unlock();
        debug("waking one by one\n");
        m.lock();
        assert (ready >= 2*N);
        m.unlock();
        for (int i=0; i < N; i++) {
            m.lock();
            cond.wake_one();
            m.unlock();
        }
        m.lock();
        while (ready < N*3) {
            done.wait(&m);
        }
        m.unlock();
    });

    t1->start();
    for (int i=0; i<N; i++) {
        threads[i]->start();
    }
    t1->join();
    delete t1;
    for (int i=0; i<N; i++) {
        threads[i]->join();
        delete threads[i];
    }
    assert_idle(&cond);

    debug("Measuring unwaited wake_all (one thread): ");
    int iterations = 100000000;
    condvar cv;
    auto start = nanotime();
    for (int i = 0; i < iterations; i++) {
        cv.wake_all();
    }
    auto end = nanotime();
    debug ("%d ns\n", (end-start)/iterations);


    debug("Measuring unwaited wake_all (two threads): ");
    iterations = 100000000;
    unsigned int nthreads = 2;
    assert(sched::cpus.size() >= nthreads);
    sched::thread *threads2[nthreads];
    std::atomic<u64> time(0);
    for(unsigned int i = 0; i < nthreads; i++) {
        threads2[i]= new sched::thread([iterations, &cv, &time] {
            auto start = nanotime();
            for (int j = 0; j < iterations; j++) {
                cv.wake_all();
            }
            auto end = nanotime();
            time += (end-start);
        }, sched::thread::attr().pin(sched::cpus[i]));
        threads2[i]->start();
    }
    for(unsigned int i = 0; i < nthreads; i++) {
        threads2[i]->join();
        delete threads2[i];
    }
    debug ("%d ns\n", time/iterations/nthreads);


    debug("condvar tests succeeded\n");
    return 0;
}
