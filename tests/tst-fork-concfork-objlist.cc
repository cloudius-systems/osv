/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for the concurrent-fork thread-stack COW fault (the
 * HammerDB-pooled fork-at-scale crash).
 *
 * Signature on an UNFIXED kernel (real KVM, SMP >= 2):
 *   [HC-FORK-DIAG] non-preemptable page_fault CR2=0x0000200000...  err=0x3
 *                  region=other pc=0 irq=0
 *   Assertion failed: sched::preemptable() (arch/x64/mmu.cc: page_fault)
 * then the guest aborts.  err=0x3 is a present+WRITE (COW) fault; CR2 is in the
 * application mmap region (0x2000...); the faulting rip is thread_main's
 * return-address push (the first stack write of a newly-scheduled thread),
 * taken with preemption AND interrupts disabled.
 *
 * ROOT CAUSE this test drives (PG-free):
 *   clone_address_space() must SHARE (never COW) every live thread's stack, or
 *   the thread's next stack write -- done with preemption/irqs off -- COW-
 *   faults illegally.  It identifies a live stack two ways: the vma's
 *   mmap_stack flag, and thread_map membership (with_all_threads()).  A pthread
 *   allocates its stack (map_anon) BEFORE the sched::thread is constructed and
 *   inserted into thread_map, and (pre-fix) without the mmap_stack flag.  So
 *   there is a window where the stack vma EXISTS but neither identifier marks
 *   it as a stack.  A fork() racing that window COW-write-protects the parent's
 *   live stack page; when the new thread is first scheduled, thread_main's push
 *   faults -> assert.
 *
 * This test maximises that window: several forker threads fork() concurrently
 * while a CHURN thread continuously creates and joins short-lived pthreads
 * (each a fresh map_anon stack) -- so at almost any instant a thread is
 * mid-construction (stack mapped, not yet in thread_map) when a fork clones the
 * address space.  Each forked child also first-touches a dlopened module's TLS
 * and hammers dladdr(), so the two program object-list vectors are exercised
 * under the children's non-preemptable rcu reads as well.
 *
 * On a FIXED kernel the pthread stack vma carries mmap_stack, so
 * clone_address_space shares it on the flag alone (independent of thread_map),
 * the window is closed, no stack page is COW-protected under a live thread, and
 * all forks + thread churn complete: the test prints PASS.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <atomic>

// Self-contained TLS-bearing .so in the tests image (no app-provided symbols):
// a single `__thread int var_lib_tls`, exports `external_library(int)`.
static const char *TLS_SO = "/tests/lib-misc-tls.so";
typedef void (*ext_lib_fn)(int);
static ext_lib_fn g_tls_fn = nullptr;
static std::atomic<int> g_stop{0};
static std::atomic<long> g_threads_churned{0};

// Force object_containing_addr() via dladdr() on several addresses so the walk
// crosses several objects in the search list (a non-preemptable _modules_rcu
// read in the forked child).
static void hammer_dladdr(int rounds)
{
    Dl_info info;
    for (int i = 0; i < rounds; i++) {
        dladdr((void *)&hammer_dladdr, &info);
        dladdr((void *)&printf, &info);
        dladdr((void *)&dlopen, &info);
        dladdr((void *)&pthread_create, &info);
    }
}

// Child-side work: FIRST touch the dlopened module's TLS (drives
// __tls_get_addr -> tls_object, a non-preemptable _module_index_list_rcu read)
// then hammer dladdr.
static void child_touch_lists(int rounds)
{
    if (g_tls_fn) {
        g_tls_fn(1);
    }
    hammer_dladdr(rounds);
}

// A short-lived churn pthread: do a trivial amount of work and exit.  Its whole
// point is the create/run/join lifecycle (a fresh map_anon stack each time),
// which is what races the fork clone's stack-sharing snapshot.
static void *churn_worker(void *)
{
    volatile int x = 0;
    for (int i = 0; i < 64; i++) x += i;
    return nullptr;
}

// The churn thread: continuously create and join a batch of short-lived
// pthreads, so at almost any instant some thread is mid-construction (stack
// mapped, not yet in thread_map) when a concurrent fork clones the AS.
static void *churn_thread(void *)
{
    const int BATCH = 8;
    pthread_t t[BATCH];
    while (!g_stop.load(std::memory_order_relaxed)) {
        int n = 0;
        for (int i = 0; i < BATCH; i++) {
            if (pthread_create(&t[n], nullptr, churn_worker, nullptr) == 0) {
                n++;
            }
        }
        for (int i = 0; i < n; i++) {
            pthread_join(t[i], nullptr);
        }
        g_threads_churned.fetch_add(n, std::memory_order_relaxed);
    }
    return nullptr;
}

// One forker thread: fork a batch of children concurrently (do NOT reap between
// forks, so many COW address spaces are live at once), each child touches both
// object lists in its own address space, then reap.  Repeat.
struct forker_arg { int rounds; int batch; int dladdr_rounds; std::atomic<int> *failures; };

static void *forker_thread(void *argp)
{
    forker_arg *a = (forker_arg *)argp;
    pid_t pids[64];
    int batch = a->batch > 64 ? 64 : a->batch;
    for (int r = 0; r < a->rounds; r++) {
        int live = 0;
        for (int i = 0; i < batch; i++) {
            pid_t p = fork();
            if (p < 0) {
                break;   // ENOMEM under heavy concurrency is a soft limit.
            }
            if (p == 0) {
                child_touch_lists(a->dladdr_rounds);
                _exit(7);
            }
            pids[live++] = p;
        }
        child_touch_lists(a->dladdr_rounds / 4 + 1);
        for (int i = 0; i < live; i++) {
            int st = 0;
            pid_t w = waitpid(pids[i], &st, 0);
            if (w != pids[i] || !WIFEXITED(st) || WEXITSTATUS(st) != 7) {
                a->failures->fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return nullptr;
}

int main(int, char **)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-concfork-objlist ===\n");

    const int NFORKERS      = 6;    // concurrent forker threads
    const int NCHURN        = 2;    // concurrent pthread-create churn threads
    const int ROUNDS        = 50;   // batches per forker
    const int BATCH         = 16;   // concurrent live children per batch
    const int DLADDR_ROUNDS = 200;  // object-list reads per child

    std::atomic<int> failures{0};

    // Keep one TLS module open for the whole run so every child always has a
    // dynamic-TLS module to first-touch (the tls_object read path).
    void *h = dlopen(TLS_SO, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        printf("NOTE: dlopen(%s) failed: %s (TLS read path not forced)\n",
               TLS_SO, dlerror());
    } else {
        g_tls_fn = reinterpret_cast<ext_lib_fn>(dlsym(h, "external_library"));
        if (!g_tls_fn) {
            printf("NOTE: dlsym(external_library) failed (TLS path not forced)\n");
        }
    }

    // Start the pthread-create churn (races the fork clone's stack snapshot).
    pthread_t churn[NCHURN];
    for (int i = 0; i < NCHURN; i++) {
        pthread_create(&churn[i], nullptr, churn_thread, nullptr);
    }

    // Start the concurrent forkers.
    pthread_t forkers[NFORKERS];
    forker_arg args[NFORKERS];
    for (int i = 0; i < NFORKERS; i++) {
        args[i] = { ROUNDS, BATCH, DLADDR_ROUNDS, &failures };
        pthread_create(&forkers[i], nullptr, forker_thread, &args[i]);
    }
    for (int i = 0; i < NFORKERS; i++) {
        pthread_join(forkers[i], nullptr);
    }
    g_stop.store(1, std::memory_order_relaxed);
    for (int i = 0; i < NCHURN; i++) {
        pthread_join(churn[i], nullptr);
    }
    if (h) {
        dlclose(h);
    }

    int total = NFORKERS * ROUNDS * BATCH;
    if (failures.load() == 0) {
        printf("PASS: %d concurrent forks (with %ld churned threads) touched the "
               "object lists cleanly\n", total, g_threads_churned.load());
        printf("SUMMARY: 0 failures\n");
        return 0;
    }
    printf("SUMMARY: %d failures of up to %d forks (%ld threads churned)\n",
           failures.load(), total, g_threads_churned.load());
    return 1;
}
