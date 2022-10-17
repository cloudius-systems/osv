/*
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <atomic>
#include <cstdlib>
#include <sys/mman.h>

static std::atomic<unsigned> tests_total(0), tests_failed(0);

void report(const char* name, bool passed)
{
    static const char* status[] = { "FAIL", "PASS" };
    printf("%s: %s\n", status[passed], name);
    tests_total += 1;
    tests_failed += !passed;
}

// Test that we can start a thread with an *unpopulated* stack. Because
// we start threads with preemption disabled (see commit
// 695375f65303e13df1b9de798577ee9a4f8f9892), there is the risk that
// the thread will start to run thread_main_c() (which finally enables
// preemption) and immediately need a page fault to populate the stack -
// which is forbidden with preemption disabled.
static bool thread_func_1_ran = false;
static void* thread_func_1(void *)
{
    thread_func_1_ran = true;
    return nullptr;
}

void test_pthread_create_unpopulated_stack()
{
    pthread_t thread;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    size_t stacksize = 65536;
    // Note MAP_STACK or MAP_POPULATE deliberately missing, so the stack
    // is not pre-populated.
    void *stackaddr = mmap(nullptr, stacksize,  
        PROT_READ | PROT_WRITE | PROT_EXEC, 
        MAP_ANONYMOUS | MAP_PRIVATE,
        -1, 0);
    report("mmap", stackaddr != MAP_FAILED);
    pthread_attr_setstack(&attr, stackaddr, stacksize);
    pthread_create(&thread, &attr, thread_func_1, nullptr);
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    report("test_pthread_create_unpopulated_stack", thread_func_1_ran == true);
}

int main(void)
{
    printf("starting pthread create test\n");

    test_pthread_create_unpopulated_stack();

    printf("SUMMARY: %u tests / %u failures\n", tests_total.load(), tests_failed.load());
    return tests_failed == 0 ? 0 : 1;
}
