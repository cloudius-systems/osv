#include "sched.hh"
#include "debug.hh"

#include <osv/condvar.h>

void assert_idle(condvar *c)
{
    assert (!c->waiters_fifo.newest);
    assert (!c->waiters_fifo.oldest);
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
    mutex m = MUTEX_INITIALIZER;
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

    debug("condvar tests succeeded\n");
    return 0;
}
