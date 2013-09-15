/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __TST_MALLOC__
#define __TST_MALLOC__

#include <osv/mutex.h>
#include "sched.hh"
#include "debug.hh"
#include <random>
#include "tst-hub.hh"


class test_malloc : public unit_tests::vtest {
public:

    struct allocme {
        char blah[1];
    };

    struct test_locks {
        mutex lock;
        std::list<struct allocme*> lista;
        bool die;
        bool alloc_finished;
        bool free_finished;
        sched::thread* main;
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
                    debug("no mem!!!!!!!!!!!11\n");
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

    void run()
    {
        test_locks t;
        t.die = t.free_finished = t.alloc_finished = false;
        t.main = sched::thread::current();
        sched::thread* t1 = new sched::thread([&] { alloc_thread(t); });
        sched::thread* t2 = new sched::thread([&] { free_thread(t); });
        t1->start();
        t2->start();

        debug("test alloc, going to sleep for 1 sec while threads are running\n");
        timespec ts = {};
        ts.tv_sec = 1;
        nanosleep(&ts, nullptr);

        t.die = true;
        t.main->wait_until([&] {return (t.alloc_finished && t.free_finished);});

        t1->join();
        t2->join();

        delete t1;
        delete t2;
        debug("Alloc test succeeded\n");
    }
};

#endif
