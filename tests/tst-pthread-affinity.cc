/*
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <atomic>
#include <sys/sysinfo.h>
#include <cstring>

#ifndef __OSV__
#define __CPU_SETSIZE	1024
#define __NCPUBITS	(8 * sizeof (__cpu_mask))

#define _NCPUWORDS	__CPU_SETSIZE / __NCPUBITS
#endif

static std::atomic<unsigned> tests_total(0), tests_failed(0);

void report(const char* name, bool passed)
{
    static const char* status[] = { "FAIL", "PASS" };
    printf("%s: %s\n", status[passed], name);
    tests_total += 1;
    tests_failed += !passed;
}

void *get_processor(void *cpuid)
{
    int *c = static_cast<int *>(cpuid);

    report("started on the correct CPU", sched_getcpu() == (*c));

    usleep(1000);

    report("re-scheduled on the correct CPU", sched_getcpu() == (*c));

    return NULL;
}

// Test that we can pin an existing thread to a specific CPU and then to unpin
// it by changing its affinity to all CPUs. Currently we test this only on the
// current thread.
void *test_pin_unpin(void *)
{
    auto ncpus = get_nprocs();
    // The thread starts with affinity to all cpus
    cpu_set_t cs;
    CPU_ZERO(&cs);
    report("getaffinity", pthread_getaffinity_np(pthread_self(), sizeof(cs), &cs) == 0);
    bool success = true;
    for (int i = 0; i < ncpus; i++) {
        success = success && CPU_ISSET(i, &cs);
    }
    report("thread is initially unpinned", success);
    // Pin the thread to cpu 0 only, and check it worked
    CPU_ZERO(&cs);
    CPU_SET(0, &cs);
    report("setaffinity", pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs) == 0);
    CPU_ZERO(&cs);
    report("getaffinity", pthread_getaffinity_np(pthread_self(), sizeof(cs), &cs) == 0);
    success = CPU_ISSET(0, &cs);
    for (int i = 1; i < ncpus; i++) {
        success = success && !CPU_ISSET(i, &cs);
    }
    report("thread is now pinned to cpu 0", success);
    // Unpin the thread (set its affinity to all CPUs) and confirm it worked
    CPU_ZERO(&cs);
    for (int i = 0; i < ncpus; i++) {
        CPU_SET(i, &cs);
    }
    report("setaffinity", pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs) == 0);
    CPU_ZERO(&cs);
    report("getaffinity", pthread_getaffinity_np(pthread_self(), sizeof(cs), &cs) == 0);
    success = true;
    for (int i = 0; i < ncpus; i++) {
        success = success && CPU_ISSET(i, &cs);
    }
    report("thread is now unpinned again", success);

    return nullptr;
}

int main(void)
{
    printf("starting pthread affinity test\n");

    cpu_set_t cs;
    pthread_t thread;
    pthread_attr_t attr;

    printf("sequential test:\n");
    for (int i = 0; i < get_nprocs(); i++) {
        CPU_ZERO(&cs);
        CPU_SET(i, &cs);
        pthread_attr_init(&attr);
        pthread_attr_setaffinity_np(&attr, sizeof(cs), &cs);
        pthread_create(&thread, &attr, get_processor, &i);
        pthread_join(thread, NULL);
        pthread_attr_destroy(&attr);
    }

    printf("parallel test:\n");
    int* cpus = new int[get_nprocs()];
    pthread_t* threads = new pthread_t[get_nprocs()];
    pthread_attr_t* attrs = new pthread_attr_t[get_nprocs()];

    for (int i = 0; i < get_nprocs(); i++) {
        cpus[i] = i;
        CPU_ZERO(&cs);
        CPU_SET(i, &cs);
        pthread_attr_init(&attrs[i]);
        pthread_attr_setaffinity_np(&attrs[i], sizeof(cs), &cs);
    }

    for (int i = 0; i < get_nprocs(); i++) {
        pthread_create(&threads[i], &attrs[i], get_processor, &cpus[i]);
    }

    for (int i = 0; i < get_nprocs(); i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < get_nprocs(); i++) {
        pthread_attr_destroy(&attrs[i]);
    }

    delete[] cpus;
    delete[] attrs;
    delete[] threads;

    cpu_set_t cs2[2];
    bool success = true;
    void *p = &cs2;

    pthread_attr_init(&attr);

    CPU_ZERO(&cs);
    CPU_SET(0, &cs);
    pthread_attr_setaffinity_np(&attr, 1, &cs);

    memset(p, -1, sizeof(cs2));
    pthread_attr_getaffinity_np(&attr, sizeof(cs2), static_cast<cpu_set_t*>(p));

    for (size_t i = 1; i < _NCPUWORDS; i++) {
        success = success && (cs2[1].__bits[i] == 0);
    }

    pthread_attr_destroy(&attr);
    report("smaller cpusetsize", success);

    success = true;
    pthread_attr_init(&attr);
    pthread_attr_getaffinity_np(&attr, sizeof(cs), &cs);
    for (size_t i = 1; i < _NCPUWORDS; i++) {
        success = success && (cs.__bits[i] == (size_t)-1);
    }
    report("All bits were set", success);

    // Test that we can pin and unpin an existing thread
    pthread_create(&thread, nullptr, test_pin_unpin, nullptr);
    pthread_join(thread, nullptr);

    printf("SUMMARY: %u tests / %u failures\n", tests_total.load(), tests_failed.load());
    return tests_failed == 0 ? 0 : 1;
}
