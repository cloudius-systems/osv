/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/clock.hh>
#include <osv/sched.hh>
#include <osv/latch.hh>
#include <lockfree/unordered-queue-mpsc.hh>
#include <lockfree/ring.hh>
#include <chrono>
#include <functional>
#include <atomic>
#include <stdio.h>
#include "stat.hh"

struct linked_object {
    linked_object* next;
};

using _clock = std::chrono::high_resolution_clock;
using queue_t = ring_spsc<void*,64*1024*1024>;

// Manages threads, allocates each thread on a different CPU
class thread_allocator
{
private:
    std::vector<sched::thread*> threads;
    int next_core {};
public:
    template<typename Func>
    void add(Func func)
    {
        assert(next_core < sched::cpus.size());
        threads.push_back(new sched::thread(func, sched::thread::attr().pin(sched::cpus[next_core++])));
    }

    void start()
    {
        for (auto t : threads) {
            t->start();
        }
    }

    void join()
    {
        for (auto t : threads) {
            t->join();
            delete t;
        }
    }

    void start_and_join()
    {
        start();
        join();
    }
};

template<typename Alloc, typename Dealloc>
void test_across_core_alloc_and_free(Alloc alloc, Dealloc dealloc)
{
    std::atomic<bool> stop(false);
    thread_barrier starting_line(3);
    latch alloc_done;

    queue_t* queue1 = new queue_t();
    queue_t* queue2 = new queue_t();

    thread_allocator threads;

    threads.add([&] {
        starting_line.arrive();

        while (!stop.load(std::memory_order_relaxed)) {
            if (!queue1->push(reinterpret_cast<linked_object*>(alloc()))) {
                break;
            }
            if (!queue2->push(reinterpret_cast<linked_object*>(alloc()))) {
                break;
            }
        }

        alloc_done.count_down();
    });

    auto freeing_worker_fn = [&] (queue_t& queue, std::atomic<long>& freed) {
        starting_line.arrive();

        auto drain_fn = [&] {
            int batch_size = 0;
            void* obj;
            while (queue.pop(obj)) {
                dealloc(obj);
                batch_size++;
                if (batch_size > 1024) {
                    freed.fetch_add(batch_size, std::memory_order_relaxed);
                    batch_size = 0;
                }
            }
            freed.fetch_add(batch_size, std::memory_order_relaxed);
        };

        while (!alloc_done.is_released()) {
            drain_fn();
        }
    };

    std::atomic<long> freed1(0);
    threads.add([&] { freeing_worker_fn(*queue1, freed1); });

    std::atomic<long> freed2(0);
    threads.add([&] { freeing_worker_fn(*queue2, freed2); });

    threads.add([&] {
        auto formatter = [] (float rate) { printf("%.2f obj/s\n", rate); };

        stat_printer printer1(freed1, formatter);
        stat_printer printer2(freed2, formatter);

        starting_line.arrive();
        auto start = _clock::now();

        if (!alloc_done.await_for(std::chrono::seconds(7))) {
            printf("Time is up! Shutting off allocation.\n");
            stop.store(true);
        }

        auto duration = to_seconds(_clock::now() - start);

        printer1.stop();
        printer2.stop();

        auto total = printer1.get_total() + printer2.get_total();
        printf("Freed %d in %.2f s = %.2f per second\n", total, duration,
            (double) total / duration);
    });

    threads.start_and_join();
}

int main(int argc, char const *argv[])
{
    test_across_core_alloc_and_free(std::bind(malloc, 1024), free);
    return 0;
}