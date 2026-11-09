/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test: fork() must not unload the running program.
 *
 * Creating or destroying a thread copies and drops a shared_ptr to the
 * program's own ELF object (sched::thread::setup_tcb, thread::thread and
 * thread::~thread).  The reference count lives in the parent's private
 * memory, which fork() write-protects for copy-on-write.  If a CPU still
 * holds a writable TLB entry for that page when another thread copies it,
 * the CPU keeps writing the old frame, which only the child maps now, and
 * the parent loses those updates.  A lost increment lets the count reach
 * zero while the program still runs: its fini functions run, its image is
 * unmapped, and the next copy faults in elf::object::run_fini_funcs().
 * PostgreSQL hit this during fork() as a fault in elf::object::soname().
 * The same lost writes also show up here as a null thread object in
 * pthread_join(), so the test fails on any of them.
 *
 * Spawner threads create and join short-lived threads on a reused stack
 * while forker threads fork; each child's exit has the reaper destroy its
 * thread.  A destructor in this program reports if its fini functions run
 * before main() returns.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <ctime>

static constexpr int nspawners = 4;
static constexpr int nforkers = 4;

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_forks{0};
static std::atomic<long> g_threads{0};
static volatile bool g_main_done = false;

__attribute__((destructor)) static void early_fini()
{
    if (!g_main_done) {
        printf("FAIL: the program's fini functions ran while main() was "
               "still running\n");
    }
}

static void *nothing(void *)
{
    return nullptr;
}

static constexpr size_t stack_size = 256 * 1024;

static void *spawner(void *)
{
    long n = 0;
    // One caller-owned stack, reused: thread creation then maps nothing, so
    // the loop does not churn the vma tree.  MAP_STACK makes fork() share it
    // rather than copy it, as it does for the stacks OSv maps itself.
    void *stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        printf("FAIL: mmap stack\n");
        return nullptr;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stack_size);
    while (!g_stop.load(std::memory_order_relaxed)) {
        pthread_t t;
        if (pthread_create(&t, &attr, nothing, nullptr) == 0) {
            pthread_join(t, nullptr);
            n++;
        }
    }
    g_threads.fetch_add(n);
    return nullptr;
}

static void *forker(void *)
{
    while (!g_stop.load(std::memory_order_relaxed)) {
        pid_t p = fork();
        if (p == 0) {
            _exit(0);
        }
        if (p > 0) {
            int st;
            waitpid(p, &st, 0);
            g_forks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    printf("=== tst-fork-thread-unload (%d s, %d spawners, %d forkers) ===\n",
           seconds, nspawners, nforkers);
    pthread_t s[nspawners], f[nforkers];
    for (auto &t : s) {
        pthread_create(&t, nullptr, spawner, nullptr);
    }
    for (auto &t : f) {
        pthread_create(&t, nullptr, forker, nullptr);
    }
    struct timespec ts = { seconds, 0 };
    nanosleep(&ts, nullptr);
    g_stop.store(true);
    for (auto &t : f) {
        pthread_join(t, nullptr);
    }
    for (auto &t : s) {
        pthread_join(t, nullptr);
    }
    printf("%ld forks, %ld threads\n", g_forks.load(), g_threads.load());
    g_main_done = true;
    if (g_forks.load() == 0) {
        printf("FAIL: no fork completed\n");
        return 1;
    }
    printf("SUMMARY: 0 failures\n");
    return 0;
}
