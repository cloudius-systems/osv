/*
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>
#include <osv/sched.hh>
#include <unistd.h>
#include <atomic>

static std::atomic<unsigned> tests_total(0), tests_failed(0);

void report(const char* name, bool passed)
{
    static const char* status[] = { "FAIL", "PASS" };
    printf("%s: %s\n", status[passed], name);
    tests_total += 1;
    tests_failed += !passed;
}

void *check_cpuset(void *ref_cs2)
{
    const cpu_set_t *ref_cs = static_cast<cpu_set_t *>(ref_cs2);
    cpu_set_t cs;
    unsigned int cpu_id;
    bool success;

    sched_getaffinity(0, sizeof(cs), &cs);
    success = true;
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        success = success && (CPU_ISSET(i, &cs) == CPU_ISSET(i, ref_cs));
    }
    report("child pinning is equal to reference cpuset", success);

    cpu_id = sched::thread::current()->get_cpu()->id;
    report("started on the correct CPU", CPU_ISSET(cpu_id, ref_cs));

    usleep(1000);

    cpu_id = sched::thread::current()->get_cpu()->id;
    report("re-scheduled on the correct CPU", CPU_ISSET(cpu_id, ref_cs));

    return NULL;
}

int main(void)
{
    printf("starting pthread affinity inherited from parent test\n");

    cpu_set_t cs, ref_cs;
    pthread_t thread;
    pthread_attr_t attr;
    bool success;
    pthread_t* threads;
    pthread_attr_t* attrs;
    cpu_set_t* ref_css;

    // we are started from httpserver, and should be unpinned.
    CPU_ZERO(&cs);
    sched_getaffinity(0, sizeof(cs), &cs);
    success = CPU_COUNT(&cs) == (int)sched::cpus.size();
    report("main is pinned to exactly sched::cpus.size cpus", success);
    success = true;
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        success = success && CPU_ISSET(i, &cs);
    }
    report("main is pinned to all cpus", success);

    // new threads should be unpinned, as cpuset requests unpinned thread
    printf("new threads from unpinned parent sequential test:\n");
    CPU_ZERO(&cs);
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_SET(i, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_init(&attr);
        pthread_attr_setaffinity_np(&attr, sizeof(cs), &cs);
        pthread_create(&thread, &attr, check_cpuset, &cs);
        pthread_join(thread, NULL);
        pthread_attr_destroy(&attr);
    }

    // new threads should be unpinned, because parent is unpinned
    printf("new threads from unpinned parent sequential test (attr with unset cpuset):\n");
    CPU_ZERO(&cs);
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_SET(i, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_init(&attr);
        pthread_create(&thread, &attr, check_cpuset, &cs);
        pthread_join(thread, NULL);
        pthread_attr_destroy(&attr);
    }

    // new threads should be unpinned, because parent is unpinned
    printf("new threads from unpinned parent sequential test (attr=NULL):\n");
    CPU_ZERO(&cs);
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_SET(i, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_create(&thread, NULL, check_cpuset, &cs);
        pthread_join(thread, NULL);
    }

    printf("new threads from unpinned parent parallel test:\n");
    threads = new pthread_t[sched::cpus.size()];
    attrs = new pthread_attr_t[sched::cpus.size()];
    CPU_ZERO(&cs);
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_SET(i, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_init(&attrs[i]);
        pthread_attr_setaffinity_np(&attrs[i], sizeof(cs), &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_create(&threads[i], &attrs[i], check_cpuset, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_destroy(&attrs[i]);
    }
    delete[] attrs;
    delete[] threads;

    printf("new threads from unpinned parent parallel test (attr with unset cpuset):\n");
    threads = new pthread_t[sched::cpus.size()];
    attrs = new pthread_attr_t[sched::cpus.size()];
    CPU_ZERO(&cs);
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_SET(i, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_init(&attrs[i]);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_create(&threads[i], &attrs[i], check_cpuset, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_destroy(&attrs[i]);
    }
    delete[] attrs;
    delete[] threads;

    printf("new threads from unpinned parent parallel test (attr=NULL):\n");
    threads = new pthread_t[sched::cpus.size()];
    CPU_ZERO(&cs);
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_SET(i, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_create(&threads[i], NULL, check_cpuset, &cs);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    delete[] threads;


    // new threads should be pinned, as cpuset requests pinned thread
    printf("new threads from pinned parent sequential test:\n");
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&cs);
        CPU_SET(i, &cs);
        pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
        pthread_attr_init(&attr);
        pthread_attr_setaffinity_np(&attr, sizeof(cs), &cs);
        pthread_create(&thread, &attr, check_cpuset, &cs);
        pthread_join(thread, NULL);
        pthread_attr_destroy(&attr);
    }
    // new thread on different CPU
    printf("new threads from pinned parent sequential test (cpuset for different cpu):\n");
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&cs);
        CPU_SET(i, &cs);
        pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
        CPU_ZERO(&ref_cs);
        CPU_SET((i + 2) % sched::cpus.size(), &ref_cs);
        pthread_attr_init(&attr);
        pthread_attr_setaffinity_np(&attr, sizeof(ref_cs), &ref_cs);
        pthread_create(&thread, &attr, check_cpuset, &ref_cs);
        pthread_join(thread, NULL);
        pthread_attr_destroy(&attr);
    }

    // new threads should be pinned, because parent is pinned
    printf("new threads from pinned parent sequential test (attr with unset cpuset):\n");
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&cs);
        CPU_SET(i, &cs);
        pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
        pthread_attr_init(&attr);
        pthread_create(&thread, &attr, check_cpuset, &cs);
        pthread_join(thread, NULL);
        pthread_attr_destroy(&attr);
    }

    // new threads should be pinned, because parent is pinned
    printf("new threads from pinned parent sequential test (attr=NULL):\n");
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&cs);
        CPU_SET(i, &cs);
        pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
        pthread_create(&thread, NULL, check_cpuset, &cs);
        pthread_join(thread, NULL);
    }


    printf("new threads from pinned parent parallel test:\n");
    threads = new pthread_t[sched::cpus.size()];
    attrs = new pthread_attr_t[sched::cpus.size()];
    ref_css = new cpu_set_t[sched::cpus.size()];
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&ref_css[i]);
        CPU_SET(i, &ref_css[i]);
        pthread_setaffinity_np(pthread_self(), sizeof(ref_css[i]), &ref_css[i]);
        pthread_attr_init(&attrs[i]);
        pthread_attr_setaffinity_np(&attrs[i], sizeof(ref_css[i]), &ref_css[i]);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_create(&threads[i], &attrs[i], check_cpuset, &ref_css[i]);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_destroy(&attrs[i]);
    }
    delete[] attrs;
    delete[] threads;
    delete[] ref_css;
    // new thread on different CPU
    threads = new pthread_t[sched::cpus.size()];
    attrs = new pthread_attr_t[sched::cpus.size()];
    ref_css = new cpu_set_t[sched::cpus.size()];
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&ref_css[i]);
        CPU_SET((i + 2) % sched::cpus.size(), &ref_css[i]);
        pthread_attr_init(&attrs[i]);
        pthread_attr_setaffinity_np(&attrs[i], sizeof(ref_css[i]), &ref_css[i]);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&cs);
        CPU_SET(i, &cs);
        pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
        pthread_create(&threads[i], &attrs[i], check_cpuset, &ref_css[i]);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_destroy(&attrs[i]);
    }
    delete[] attrs;
    delete[] threads;
    delete[] ref_css;

    printf("new threads from pinned parent parallel test (attr with unset cpuset):\n");
    threads = new pthread_t[sched::cpus.size()];
    attrs = new pthread_attr_t[sched::cpus.size()];
    ref_css = new cpu_set_t[sched::cpus.size()];
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_init(&attrs[i]);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&ref_css[i]);
        CPU_SET(i, &ref_css[i]);
        pthread_setaffinity_np(pthread_self(), sizeof(ref_css[i]), &ref_css[i]);
        pthread_create(&threads[i], &attrs[i], check_cpuset, &ref_css[i]);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_attr_destroy(&attrs[i]);
    }
    delete[] attrs;
    delete[] threads;
    delete[] ref_css;

    printf("new threads from pinned parent parallel test (attr=NULL):\n");
    threads = new pthread_t[sched::cpus.size()];
    ref_css = new cpu_set_t[sched::cpus.size()];
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        CPU_ZERO(&ref_css[i]);
        CPU_SET(i, &ref_css[i]);
        pthread_setaffinity_np(pthread_self(), sizeof(ref_css[i]), &ref_css[i]);
        pthread_create(&threads[i], NULL, check_cpuset, &ref_css[i]);
    }
    for (size_t i = 0; i < sched::cpus.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    delete[] threads;
    delete[] ref_css;

    printf("SUMMARY: %u tests / %u failures\n", tests_total.load(), tests_failed.load());
    return tests_failed == 0 ? 0 : 1;
}
