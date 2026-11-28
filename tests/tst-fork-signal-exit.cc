/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A signal handler running in a fork child's address space while the child
// exits and is reaped.  OSv runs a handler on a new thread placed in the
// target's address space.  The parent sends a stream of SIGUSR2 to each child,
// and the child exits while handlers still run.  Each handler allocates,
// writes its allocation and the child's globals, and sleeps briefly, so it
// outlives the child's own thread, and allocates again after the sleep.  The
// address space must stay mapped until the last handler is done: 200
// children, then nothing may have faulted, every child must be reaped with its
// own exit status, and no arena state may outlive its address space.
//
// With an execve argument, each of 30 children execs a payload instead of
// exiting, while handlers run in the address space it left.  Each exec takes
// one of the 31 free ELF namespaces, which are not reused, hence 30.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-signal-exit requires CONF_fork\n");
    return 0;
}

#else

#include <osv/fork_arena.hh>
#include <atomic>
#include <new>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

static const int nchildren_exit = 200;
static const int nchildren_exec = 30;
static const int burst = 40;

struct shared {
    std::atomic<int> ready;
    std::atomic<long> handled;
    std::atomic<long> bad;
};
static shared *sh;
static volatile long child_counter;
static volatile unsigned long child_word = 0x5afe5afe;

static void handler(int)
{
    // Allocate and write in the child's heap, then outlive the child thread.
    auto p = static_cast<unsigned long*>(malloc(256));
    if (p) {
        for (int i = 0; i < 32; i++) {
            p[i] = child_word + i;
        }
        for (int i = 0; i < 32; i++) {
            if (p[i] != child_word + i) {
                sh->bad++;
            }
        }
    }
    child_counter++;
    struct timespec ts{0, 2 * 1000 * 1000};
    nanosleep(&ts, nullptr);
    if (p && p[5] != child_word + 5) {
        sh->bad++;
    }
    free(p);
    free(malloc(64));
    sh->handled++;
}

int main(int argc, char **argv)
{
    bool exec = argc > 1 && !strcmp(argv[1], "execve");
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);

    void *m = mmap(nullptr, sizeof(shared), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        printf("FAIL: mmap\n");
        return 1;
    }
    sh = new (m) shared{};
    sleep(1);
    unsigned long states0 = fork_arena::stats().live_states;
    int failures = 0, wrong_status = 0, unreaped = 0;
    int nchildren = exec ? nchildren_exec : nchildren_exit;
    for (int i = 0; i < nchildren; i++) {
        sh->ready = 0;
        pid_t c = fork();
        if (c == 0) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = handler;
            sigaction(SIGUSR2, &sa, nullptr);
            sh->ready = 1;
            // Let a few handlers start, then leave while more are arriving.
            while (child_counter == 0) {
                sched_yield();
            }
            if (exec) {
                char *const args[] = { (char*)"/tests/payload-exit7.so", nullptr };
                char *const envp[] = { nullptr };
                execve(args[0], args, envp);
                _exit(99);
            }
            _exit(i & 0x7f);
        }
        if (c < 0) {
            printf("FAIL: fork %d: errno %d\n", i, errno);
            return 1;
        }
        while (!sh->ready.load()) {
            sched_yield();
        }
        for (int k = 0; k < burst; k++) {
            if (kill(c, SIGUSR2) != 0 && errno != ESRCH) {
                printf("FAIL: kill: errno %d\n", errno);
                failures++;
                break;
            }
        }
        int st = 0;
        if (waitpid(c, &st, 0) != c) {
            unreaped++;
            continue;
        }
        int want = exec ? 7 : (i & 0x7f);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != want) {
            if (!wrong_status) {
                printf("child %d: status 0x%x, want exit %d\n", i, st, want);
            }
            wrong_status++;
        }
    }
    // Handlers may outlive their child: give the last ones time to finish.
    sleep(1);
    long states = long(fork_arena::stats().live_states) - long(states0);
    printf("%d children, %ld handlers ran, %ld bad words, %d wrong status, %d not reaped, "
           "%ld arena states left\n", nchildren, sh->handled.load(), sh->bad.load(),
           wrong_status, unreaped, states);
    if (sh->bad || wrong_status || unreaped || states > 0) {
        printf("FAIL: tst-fork-signal-exit\n");
        failures++;
    }
    if (!failures) {
        printf("PASS: tst-fork-signal-exit%s\n", exec ? " execve" : "");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
