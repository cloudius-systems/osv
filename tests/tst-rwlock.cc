/*
 * Copyright (C) 2025 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

//This test is loosely based on the similar test in Go referenced by
//https://eli.thegreenplace.net/2019/implementing-reader-writer-locks/
//and implemented here - https://github.com/eliben/code-for-blog/blob/main/2019/rwlocks/rwlock_test.go
#include <osv/sched.hh>
#include <osv/rwlock.h>
#include <osv/clock.hh>
#include <atomic>
#include <iostream>

#include <boost/program_options.hpp>

static std::atomic<u64> reader_count(0);
static std::atomic<s64> reader_lock_wait_total(0);
static std::atomic<s64> reader_unlock_wait_total(0);
static std::atomic<s64> reader_work_total(0);
static std::atomic<u64> writer_count(0);
static std::atomic<s64> writer_lock_wait_total(0);
static std::atomic<s64> writer_unlock_wait_total(0);
static std::atomic<s64> writer_work_total(0);

rwlock *rwlock_ref;

static void writer_thread(int id, rwlock &lock, long iterations, int data[], long data_len,
                          int inc, int loops, bool writer_sleep, bool sleep_before)
{
    for (int i = 0; i < iterations; i++) {
        if (sleep_before) {
            //Sleep 10 - 80 microseconds
            usleep((1 + random() % 8) * 10);
        }

        //time now
        auto before_wlock = clock::get()->time();
        lock.wlock();

        //Measure time to acquire lock
        auto after_wlock = clock::get()->time();
        //
        //Write
        for (int l = 0; l < loops; l++) {
            for (int j = 0; j < data_len; j++)
                data[j] += inc;
        }
        if (writer_sleep) {
            //Sleep 1 - 10 microseconds
            usleep((1 + random() % 10));
        }

        //Measure cost of wunlock() - may be expensive with many waiting readers
        auto before_wunlock = clock::get()->time();

        //Unlock
        lock.wunlock();
        auto after_wunlock = clock::get()->time();
        //
        //Update stats
        writer_count.fetch_add(1, std::memory_order_relaxed);
        writer_lock_wait_total.fetch_add(after_wlock - before_wlock, std::memory_order_relaxed);
        writer_unlock_wait_total.fetch_add(after_wunlock - before_wunlock, std::memory_order_relaxed);
        writer_work_total.fetch_add(before_wunlock - after_wlock, std::memory_order_relaxed);
     }
}

static void reader_thread(int id, rwlock &lock, long iterations, int data[], long data_len, bool sleep_before)
{
    for (int i = 0; i < iterations; i++) {
        if (sleep_before) {
            //Sleep 10 - 80 microseconds
            usleep((1 + random() % 8) * 10);
        }
        //
        //time now
        auto before_rlock = clock::get()->time();
        lock.rlock();

        //Measure time to acquire lock
        auto after_rlock = clock::get()->time();
        //
        //Read and verify
        for (int j = data_len - 1; j > 0; j--)
            assert(data[j] == (data[j-1] + 1));
        //
        //Measure cost of runlock() - should be cheap in general
        auto before_runlock = clock::get()->time();

        //Unlock
        lock.runlock();
        auto after_runlock = clock::get()->time();

        //
        //Update stats
        reader_count.fetch_add(1, std::memory_order_relaxed);
        reader_lock_wait_total.fetch_add(after_rlock - before_rlock, std::memory_order_relaxed);
        reader_unlock_wait_total.fetch_add(after_runlock - before_runlock, std::memory_order_relaxed);
        reader_work_total.fetch_add(before_runlock - after_rlock, std::memory_order_relaxed);
     }
}

struct params {
    unsigned readers;
    unsigned writers;
    unsigned writer_loops;
    unsigned iterations;
    unsigned data_len;
    bool writer_sleep;
    bool sleep_before;
};

static void test(params& p, bool pinned)
{
    printf("Test with %d writers and %d readers, %d iterations, %d writer inner loops, data len=%d, %spinned threads\n",
            p.writers, p.readers, p.iterations, p.writer_loops, p.data_len, pinned ? "" : "non-");

    reader_count.store(0);
    reader_lock_wait_total.store(0);
    reader_unlock_wait_total.store(0);
    writer_count.store(0);
    writer_lock_wait_total.store(0);
    writer_unlock_wait_total.store(0);

    //Initialize the data
    int *data = new int[p.data_len]; 
    for (unsigned j = 0; j < p.data_len; j++)
        data[j] = j;

    rwlock rw_lock;
    rwlock_ref = &rw_lock;

    int all_threads = p.readers + p.writers;
    sched::thread **threads = new sched::thread *[all_threads];

    for(unsigned i = 0; i < p.readers; i++) {
        threads[i] = sched::thread::make([i, &rw_lock, &p, data] {
            reader_thread(i, rw_lock, p.iterations, data, p.data_len, p.sleep_before);
        }, pinned ? sched::thread::attr().pin(sched::cpus[i % sched::cpus.size()]) : sched::thread::attr());
    }

    for(unsigned i = 0; i < p.writers; i++) {
        threads[p.readers + i] = sched::thread::make([i, &rw_lock, &p, data] {
            writer_thread(i, rw_lock, p.iterations, data, p.data_len, i + 1, p.writer_loops, p.writer_sleep, p.sleep_before);
        }, pinned ? sched::thread::attr().pin(sched::cpus[i % sched::cpus.size()]) : sched::thread::attr());
    }

    auto t1 = clock::get()->time();
    for(int i = 0; i < all_threads; i++) {
        threads[i]->start();
    }
    for(int i = 0; i < all_threads; i++){
        threads[i]->join();
        delete threads[i];
    }

    auto t2 = clock::get()->time();

    constexpr long ns_per_ms = 1000000;
    printf("Reader lock/unlock count = %d, lock wait: total = %.2f ms, avg = %.2f ns, unlock wait: total = %.2f ms, avg = %.2f ns, work: total = %.2f ms, avg = %.2f ns\n",
        reader_count.load(), ((double)reader_lock_wait_total.load()) / ns_per_ms, ((double)reader_lock_wait_total.load()) / reader_count.load(),
        ((double)reader_unlock_wait_total.load()) / ns_per_ms, ((double)reader_unlock_wait_total.load()) / reader_count.load(),
        ((double)reader_work_total.load()) / ns_per_ms, ((double)reader_work_total.load()) / reader_count.load());
    printf("Writer lock/unlock count = %d, lock wait: total = %.2f ms, avg = %.2f ns, unlock wait: total = %.2f ms, avg = %.2f ns, work: total = %.2f ms, avg = %.2f ns\n",
        writer_count.load(), ((double)writer_lock_wait_total.load()) / ns_per_ms, ((double)writer_lock_wait_total.load()) / writer_count.load(),
        ((double)writer_unlock_wait_total.load()) / ns_per_ms, ((double)writer_unlock_wait_total.load()) / writer_count.load(),
        ((double)writer_work_total.load()) / ns_per_ms, ((double)writer_work_total.load()) / writer_count.load());
    printf("Total runtime %.2f ms\n", (((double)t2 - t1) / ns_per_ms));
    printf("\n");

    delete [] data;
}

int main(int argc, char **argv)
{
    printf("Running contended read-write lock tests\n\n");

    namespace bpo = boost::program_options;
    params p;

    bpo::options_description desc("tst-tcp options");
    desc.add_options()
        ("help", "show help text")
        ("readers,r", bpo::value(&p.readers)->default_value(1000),
                "number of reader threads")
        ("writers,w", bpo::value(&p.writers)->default_value(10),
                "number of writer threads")
        ("wloops,l", bpo::value(&p.writer_loops)->default_value(1),
                "number of writer internal loops")
        ("data_len,d", bpo::value(&p.data_len)->default_value(1000),
                "size of the test array that is read from and written to")
        ("iterations,i", bpo::value(&p.iterations)->default_value(100),
                "number of times each reader reads from and writer writes to the test array")
        ("wsleep,s", bpo::value(&p.writer_sleep)->default_value(false),
                "should writer sleep randomly (1-10 us) before unlock")
        ("sleep,b", bpo::value(&p.sleep_before)->default_value(true),
                "should reader and writer sleep randomly (10-80 us) before lock")
    ;
    bpo::variables_map vars;
    bpo::store(bpo::parse_command_line(argc, argv, desc), vars);
    bpo::notify(vars);

    if (vars.count("help")) {
        std::cout << desc << "\n";
        exit(1);
    }
     
    test(p, false);
    test(p, true);
}
