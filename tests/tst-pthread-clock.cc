/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test POSIX thread clock functionality
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <thread>
#include <iostream>
#include <assert.h>
#include <unistd.h>
#include <ctime>

// This is so that thread eventually terminates. We will flip it to false
// when we're done. Volatile shouldn't be required, but gcc is optimizing
// the entire loop out if it isn't here
static volatile bool run_subthread = true;

static void *
thread_start(void *arg)
{
    while (run_subthread);
    return nullptr;
}

static unsigned long
pclock(clockid_t cid)
{
    struct timespec ts;
    assert(clock_gettime(cid, &ts) != -1);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void pass_if(bool cond, const char *msg)
{
    if (!cond) {
        std::cerr << "ERROR: ";
        std::cerr << msg;
        std::cerr << "\n";
        exit(1);
    }
}

int main(int ac, char** av)
{
    pthread_t thread;
    clockid_t cid, ccid;
    int s;

    // This make sure that the process clock is monotonic.  If we were sure we
    // are the first (or only) test to run, we could verify that this also
    // corresponds to total uptime. But without that guarantee, better to leave
    // it alone. If you suspect something is busted in that aspect, please edit
    // this test to manually verify it.
    auto uptime = pclock(CLOCK_PROCESS_CPUTIME_ID);
    // First in a tight cpu loop
    for (int i = 0; i < 100000; ++i) {
        auto tmp = pclock(CLOCK_PROCESS_CPUTIME_ID);
        pass_if(tmp >= uptime, "Process CPU time not monotonic");
        uptime = tmp;
    }

    // And then after sleeping
    sleep(1);
    pass_if(pclock(CLOCK_PROCESS_CPUTIME_ID) >= uptime, "Process CPU time not monotonic after sleep");

    // Since we are computing a thread clock, time spent sleeping should not add
    // to our runtime. In practice, we'll account *some* time, but that should be
    // negligible. We'll accept results with up to 1 % difference.
    auto start = pclock(CLOCK_THREAD_CPUTIME_ID);
    sleep(1);
    auto finish = pclock(CLOCK_THREAD_CPUTIME_ID);
    pass_if(((finish - start) * 100) / start <= 1, "Accounted time while sleeping");

    // Now we verify the opposite: We cannot guarantee this thread will be
    // scheduled at all times, but it should be doing work and consuming CPU
    // most of the time. Let's make sure the thread clock advances by at least
    // half of the amount of wallclock we have. In practice it should be much
    // more, but in a loaded hypervisor, for instance, it can go lower.
    auto base = pclock(CLOCK_REALTIME);
    auto br = base;
    start = pclock(CLOCK_THREAD_CPUTIME_ID);
    for (;;) {
        br = pclock(CLOCK_REALTIME);
        if ((br - base) > 1000000000)
            break;
    }
    auto t = pclock(CLOCK_THREAD_CPUTIME_ID) - start;
    auto wall = br - base;
    pass_if((t * 100) / wall > 50, "Time not being spent by main thread");

    s = pthread_create(&thread, NULL, thread_start, NULL);
    assert(s == 0);
    sleep(1);

    s = pthread_getcpuclockid(pthread_self(), &cid);
    assert(s == 0);
    s = pthread_getcpuclockid(thread, &ccid);
    assert(s == 0);

    // Asking for CLOCK_THREAD_CPUTIME_ID or asking for a specific thread
    // with our id should yield the same results. Acceptable differences
    // are only due to callers being separated in time.
    auto self = pclock(CLOCK_THREAD_CPUTIME_ID);
    auto selfid = pclock(cid);

    // This is the child thread. It ran while we slept for 1 second.
    // We won't run for the full second, but should be close to it.
    auto child = pclock(ccid);

    pass_if((child * 100) / 1000000000 > 80, "Child thread ran for a lot less than one second");
    pass_if((self * 100) / selfid > 98, "thread clock mismatch");

    run_subthread = false;
    pthread_join(thread, nullptr);

    std::cerr << "PASSED\n";

    return 0;
}
