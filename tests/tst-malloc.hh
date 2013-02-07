#include "mutex.hh"
#include <random>

struct allocme {
    char blah[1];
};

struct test_locks {
    ::mutex lock;
    std::list<struct allocme*> lista;
    bool die;
    bool alloc_finished;
    bool free_finished;
    thread* main;
};

void alloc_thread(test_locks &t)
{
    int i = 0;
    std::default_random_engine generator(0);
    std::uniform_int_distribution<int> distribution(1,100);

    while (!t.die) {
        t.lock.lock();
        while (distribution(generator) != 100) {
            i++;
            allocme* mem = new allocme;
            if (!mem) {
                debug("no mem!!!!!!!!!!!11");
                break;
            }
            mem->blah[0] = (char)i;
            t.lista.push_back(mem);
        }
        t.lock.unlock();
        sched::thread::current()->yield();
    }

    //debug(fmt("alloc thread finished, allocated %d obj") % i);
    t.alloc_finished = true;
    t.main->wake();
}

void free_thread(test_locks &t)
{
    while (!t.die) {
        t.lock.lock();
        while (!t.lista.empty()) {
            volatile char blah;
            allocme *mem = t.lista.front();
            blah = mem->blah[0];
            delete mem;
            t.lista.pop_front();
            //dummy for the compiler
            if (blah == 0) continue;
        }
        t.lock.unlock();
        sched::thread::current()->yield();
    }
    //debug("free thread done");
    t.free_finished = true;
    t.main->wake();
}

void test_alloc()
{
    test_locks t;
    t.die = t.free_finished = t.alloc_finished = false;
    t.main = sched::thread::current();
    thread* t1 = new thread([&] { alloc_thread(t); });
    thread* t2 = new thread([&] { free_thread(t); });

    debug("test alloc, going to sleep for 1 sec while threads are running");
    timespec ts = {};
    ts.tv_sec = 1;
    nanosleep(&ts, nullptr);

    t.die = true;
    t.main->wait_until([&] {return (t.alloc_finished && t.free_finished);});

    delete t1;
    delete t2;
    debug("Alloc test succeeded");
}
