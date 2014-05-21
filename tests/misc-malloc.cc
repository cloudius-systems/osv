/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <chrono>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <random>
#include <string>
#include <functional>
#include <thread>
#include <numeric>
#include <vector>
#include <cmath>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <cstdlib>

unsigned int threads = 2;
using namespace std::chrono;
static high_resolution_clock s_clock;
constexpr int loops = 2000;
constexpr int runs = 100;

struct result {
    float malloc;
    float free;
};

class thread_data {
public:
    explicit thread_data(long nthreads, std::function<long ()> len, long tf);
    void measure(std::function<long ()> len, long tf);
    void join() { thread.join(); }

    unsigned long nthreads;
    void *mallocs[loops];
    float malloc_time;
    float free_time;
    std::thread thread;
};

thread_data::thread_data(long nthreads, std::function<long()> len, long tf)
    : nthreads(nthreads), thread([&] { measure(len, tf); })
{
}

static unsigned long ready_to_free = 0;
static std::condition_variable cond;
static std::mutex notify_mutex;
typedef std::shared_ptr<thread_data> thread_ptr;
static std::vector<thread_ptr> thread_vec;

void thread_data::measure(std::function<long ()> len, long tf)
{
    std::unique_lock<std::mutex> lk(notify_mutex);
    cond.wait(lk, [&]{ return thread_vec.size() == nthreads; });
    lk.unlock();

    auto t1 = s_clock.now();
    for (int i=0; i<loops; i++) {
        mallocs[i] = malloc(len());
    }
    auto t2 = s_clock.now();

    lk.lock();
    ready_to_free += 1;
    lk.unlock();

    cond.notify_all();

    lk.lock();
    cond.wait(lk, [&] { return ready_to_free == nthreads; });
    lk.unlock();

    auto t3 = s_clock.now();
    for (int i=0; i<loops; i++) {
        free(thread_vec[tf]->mallocs[i]);
    }
    auto t4 = s_clock.now();

    malloc_time = ((float)duration_cast<nanoseconds>(t2-t1).count()) / loops;
    free_time =   ((float)duration_cast<nanoseconds>(t4-t3).count()) / loops;
}

static void do_measure(std::function<long ()> len, long t_dest, long nthreads)
{
    thread_vec.clear();

    std::unique_lock<std::mutex> lk(notify_mutex);
    ready_to_free = 0;

    for (long tm = 0; tm < nthreads; ++tm) {
        long tf = (tm + t_dest) % nthreads;
        thread_ptr tptr(new thread_data(nthreads, len, tf));
        thread_vec.push_back(tptr);
    }
    lk.unlock();

    cond.notify_all();

    for (auto &t: thread_vec) {
        t->join();
    }
}

static void measure_up(std::function<long ()> len)
{
    do_measure(len, 0, 1);
}

static void measure_smp(std::function<long ()> len, int t_dest)
{
    do_measure(len, t_dest, threads);
}

static void measure_smp_colocated(std::function<long ()> len)
{
    measure_smp(len, 0);
}

static void measure_smp_cross(std::function<long ()> len)
{
    measure_smp(len, 1);
}

void do_run(std::function <void ()> fn, std::string name)
{
    struct std::vector<float> m;
    struct std::vector<float> f;

    for (int i= 0; i < runs; ++i) {
        fn();
        for (auto thr : thread_vec) {
            m.push_back(thr->malloc_time);
            f.push_back(thr->free_time);
        }
    }
    auto stdev = [](std::vector<float> v, float mean) {
        float accum = 0.0;
        std::for_each (std::begin(v), std::end(v), [&](const float d) {
            accum += (d - mean) * (d - mean);
        });
        return sqrt(accum / (v.size()));
    };

    float mmin = *std::min_element(m.begin(), m.end());
    float mmax = *std::max_element(m.begin(), m.end());
    float mmean = std::accumulate(std::begin(m), std::end(m), 0.0) / m.size();
    float mstdev = stdev(m, mmean);

    float fmin = *std::min_element(f.begin(), f.end());
    float fmax = *std::max_element(f.begin(), f.end());
    float fmean = std::accumulate(std::begin(f), std::end(f), 0.0) / f.size();
    float fstdev = stdev(f, fmean);

    std::cout << name << ",malloc," << mmin << "," << mmax << "," << mmean << "," << mstdev << "\n";
    std::cout << name << ",free,"   << fmin << "," << fmax << "," << fmean << "," << fstdev << "\n";
}

static constexpr long up_max = 1 << 20;
static constexpr long smp_max = 256 << 10;

int main(int argc, char **argv)
{
    std::default_random_engine generator;
    std::uniform_int_distribution<unsigned> distribution(8, up_max);
    std::uniform_int_distribution<unsigned> smp_distribution(8, smp_max);

    if (argc > 1) {
        threads = atoi(argv[1]);
    }

    for (long i = 8; i <= up_max; i <<= 1) {
        do_run([&] { measure_up([&] { return i; }); }, "up," + std::to_string(i));
    }
    do_run([&] { measure_up([&] { return distribution(generator); }); },  "up,random");

    for (long i = 8; i <= smp_max; i <<= 1) {
        do_run([&] { measure_smp_colocated([&] { return i; }); }, "smp," + std::to_string(i));
    }
    do_run([&] { measure_smp_colocated([&] { return smp_distribution(generator); }); },  "smp,random");

    for (long i = 8; i <= smp_max; i <<= 1) {
        do_run([&] { measure_smp_cross([&] { return i; }); }, "smpcross," + std::to_string(i));
    }
    do_run([&] { measure_smp_cross([&] { return smp_distribution(generator); }); },  "smpcross,random");
}
