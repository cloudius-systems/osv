/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for the fork+reap SUSTAINED-SERVING wall.
 *
 * PostgreSQL's postmaster serves each client connection by fork()ing a fresh
 * backend, which runs a query and exit()s; the postmaster then reaps it
 * (SIGCHLD / waitpid).  Under that fork -> work -> exit -> reap cycle -- with a
 * rolling window of overlapping backends, exactly as a postmaster has -- stock
 * PG on OSv served the first several connections and then trapped a general
 * protection fault deep in the NEXT fork():
 *
 *   general protection fault
 *     std::__shared_count<>::__shared_count(const __weak_count&)   <- copy ref
 *     osv::application::get_shared()          (shared_from_this)
 *     osv::application::get_current()         core/app.cc
 *     sched::thread::thread(...)              ctor: _app_runtime = app->runtime()
 *     fork_thread(...) / fork()
 *
 * ROOT CAUSE: the application_runtime object (and its shared_ptr control block)
 * were allocated by an APP thread at postmaster start, so std_malloc routed
 * them into the COW fork arena (VA 0x3000..).  Every backend thread's
 * _app_runtime points at that arena object.  clone_address_space() COW-clones
 * the arena per fork; across fork/reap cycles the fork()'ing thread reads a
 * DIVERGENT arena copy of application_runtime whose `app` reference field has
 * been clobbered to garbage (gdb: runtime->app == 0x202000ae4bc0210, a wild
 * pointer that VARIED run-to-run -- classic COW divergence), so
 * get_current()->get_shared()->shared_from_this() dereferences a bogus
 * application* and GP-faults.  gdb confirmed _app_runtime._M_ptr ==
 * 0x3000000004a0 (in the arena) and its control block at 0x3000000004e0.
 *
 * THE FIX (core/app.cc): allocate application_runtime + its control block on
 * the identity kernel heap (make_shared under fork_arena::kernel_heap_scope),
 * so the runtime is byte-identical in every address space and never diverges
 * -- the same rule already applied to the DSM registry, mbufs, thread objects
 * and the thread kernel stack.
 *
 * This test reproduces the wall WITHOUT PostgreSQL by mirroring the postmaster:
 * keep a rolling window of overlapping live children while continuously
 * fork()ing new ones and reaping the oldest, for many iterations.  Each child
 * does a little real work (touch heap, touch a .data global, a syscall, heap
 * churn) and exit()s with a distinct known code the parent waitpid-verifies.
 *
 * HONEST NOTE ON REPRODUCTION: the DEFINITIVE reproducer of the GP fault is
 * PostgreSQL itself (~6-7 sequential connections; gdb backtrace above).  This
 * synthetic loop does NOT by itself corrupt the arena runtime, because the
 * arena bump allocator hands application_runtime a stable low VA that is never
 * recycled, and COW divergence only touches pages a process actually WRITES --
 * and nothing here writes the runtime's page.  The PG corruption additionally
 * involves the network/DSM/signal write patterns a postmaster exercises.  This
 * test is therefore the OS-level GUARD for the fixed code path (sustained
 * overlapping fork/work/exit/reap with get_current() on every new-thread ctor);
 * it must stay green, and it is fast and PG-free.  It PASSES with the fix; the
 * regression it guards against is any change that reintroduces a get_current()
 * fault on the common fork path.
 *
 * To reproduce (in arena-dev, CONF_fork=y build):
 *   ./scripts/build conf_fork=1 fs=rofs image=tests, boot with -smp 2
 *   (fork needs >=2 vCPUs on this build):
 *   qemu ... --rootfs=rofs /tests/tst-fork-serial.so  (-smp 2)
 */

#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

static const int ITERATIONS = 30;   // >> the ~6 PG served before the wall
static const int WINDOW = 3;        // rolling overlapping live children

// A .data global each child touches, to exercise a real write in the child's
// COW-private address space (the class of access that broke the arena runtime).
static volatile int g_touch = 0x1234;

// Child body: a little real work like a PG backend, plus heap churn.  Exits
// with a distinct verifiable code (kept in 0..127).
static void child_body(int i)
{
    int sum = 0;
    for (int round = 0; round < 32; round++) {
        int *heap = (int *)malloc(256 * sizeof(int));
        if (!heap) {
            _exit(200);
        }
        for (int k = 0; k < 256; k++) {
            heap[k] = i * 1000 + k + round;
        }
        for (int k = 0; k < 256; k++) {
            sum += heap[k];
        }
        // clobber with a path-like string (as PG's recycled block link was)
        snprintf((char *)heap, 32, "/backend/%d/round.%d", i, round);
        free(heap);
    }
    g_touch = i + sum;            // COW write to .data
    (void)getpid();               // a syscall (kernel code on the app stack)
    _exit((i + 1) & 0x7f);
}

// Reap one specific child and verify its exit code == want.  Returns 0 on ok.
static int reap_verify(pid_t pid, int want)
{
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    if (w != pid) {
        printf("FAIL: waitpid returned %d (expected %d) errno=%d\n",
               (int)w, (int)pid, errno);
        return 1;
    }
    if (!WIFEXITED(status)) {
        printf("FAIL: child %d did not exit normally (status=0x%x)\n",
               (int)pid, status);
        return 1;
    }
    int code = WEXITSTATUS(status);
    if (code != want) {
        printf("FAIL: child %d exit code %d, expected %d\n",
               (int)pid, code, want);
        return 1;
    }
    return 0;
}

int main()
{
    // Unbuffered: a fork child's _exit does not flush shared stdio on OSv.
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-serial ===\n");

    pid_t live_pid[WINDOW];
    int   live_code[WINDOW];
    int   nlive = 0;
    int   passed = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        // Postmaster churns its OWN arena heap between forks (work between
        // accepting connections); on HEAD this recycles/COW-diverges arena
        // chunks near the runtime.
        void *keep[64];
        for (int k = 0; k < 64; k++) {
            keep[k] = malloc(48 + (k & 31) * 8);
            if (keep[k]) {
                snprintf((char *)keep[k], 24, "/pm/%d/slot.%d", i, k);
            }
        }
        for (int k = 0; k < 64; k++) {
            free(keep[k]);
        }

        pid_t pid = fork();
        if (pid < 0) {
            printf("FAIL: fork() iteration %d errno=%d\n", i, errno);
            return 1;
        }
        if (pid == 0) {
            child_body(i);   // never returns
        }

        // Parent: register the new child in the rolling window.  If the window
        // is full, reap the OLDEST first (overlapping live children, like PG).
        if (nlive == WINDOW) {
            if (reap_verify(live_pid[0], live_code[0])) {
                return 1;
            }
            passed++;
            printf("iteration %d: reaped pid=%d code=%d (ok)\n",
                   i - WINDOW, (int)live_pid[0], live_code[0]);
            for (int s = 1; s < WINDOW; s++) {
                live_pid[s - 1] = live_pid[s];
                live_code[s - 1] = live_code[s];
            }
            nlive--;
        }
        live_pid[nlive] = pid;
        live_code[nlive] = (i + 1) & 0x7f;
        nlive++;
    }

    // Drain the remaining live children.
    for (int s = 0; s < nlive; s++) {
        if (reap_verify(live_pid[s], live_code[s])) {
            return 1;
        }
        passed++;
        printf("drain: reaped pid=%d code=%d (ok)\n",
               (int)live_pid[s], live_code[s]);
    }

    if (passed != ITERATIONS) {
        printf("FAIL: only %d/%d fork/reap iterations succeeded\n",
               passed, ITERATIONS);
        return 1;
    }
    printf("PASS: %d/%d overlapping fork/work/exit/reap iterations succeeded "
           "-- no corrupt _app_runtime GP fault (sustained-serving wall)\n",
           passed, ITERATIONS);
    return 0;
}
