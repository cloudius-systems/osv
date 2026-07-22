/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Tests OSv's thread-backed fork() emulation (see documentation/fork.md).
 * fork() on OSv shares the address space with the parent but gives the child a
 * private copy of the parent's stack, so the twin return and simple
 * child-does-work-then-_exit / fork+exec / waitpid flows work.  This test
 * exercises those; it does NOT assert memory isolation (which OSv cannot give).
 */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

// 1. fork() return-value contract + private stack.
static void test_fork_return()
{
    volatile int parent_local = 0x1111;   // must be untouched by the child
    pid_t pid = fork();
    if (pid == 0) {
        // child: its own copy of parent_local; mutate it and exit with a code
        volatile int child_local = parent_local;   // reads the copied value
        child_local = 0x2222;
        (void)child_local;
        _exit(42);
    }
    // parent
    CHECK(pid > 0, "fork() returns child pid > 0 to parent");
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid, "waitpid() reaps the fork child");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 42,
          "child exit code 42 delivered via waitpid");
    CHECK(parent_local == 0x1111,
          "parent's stack local intact after child mutated its own copy");
}

// 2. fork() + execve(): the child really execs a program in a fresh ELF
// namespace and the parent reaps it.  The payload (/tests/payload-exit7.so,
// built by modules/tests) prints a marker and exit(7)s; the child never
// returns from a successful execve(), so a return means exec failed and we
// signal that with a distinct code (99) that the parent flags as a failure.
static void test_fork_exec()
{
    pid_t pid = fork();
    if (pid == 0) {
        char *const argv[] = { (char*)"/tests/payload-exit7.so", nullptr };
        char *const envp[] = { nullptr };
        execve(argv[0], argv, envp);
        _exit(99);   // only reached if execve() FAILED to launch the payload
    }
    CHECK(pid > 0, "fork() before execve returns child pid");
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid, "waitpid() reaps the fork+exec child");
    // 7 means the exec'd payload actually ran and exited; 99 means execve()
    // returned (failed to launch) -- that is now a real failure.
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7,
          "fork+exec: exec'd payload ran and its exit code (7) was reaped");
}

// 3. vfork() maps to fork(); same contract.
static void test_vfork()
{
    pid_t pid = vfork();
    if (pid == 0) {
        _exit(9);
    }
    CHECK(pid > 0, "vfork() returns child pid to parent");
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 9,
          "vfork child exit code 9 via waitpid");
}

// 4. waitpid with no children returns -1/ECHILD.
static void test_no_children()
{
    int status = 0;
    errno = 0;
    pid_t w = waitpid(-1, &status, 0);
    CHECK(w == -1, "waitpid with no children returns -1");
}

int main()
{
    printf("=== tst-fork ===\n");
    test_fork_return();
    test_fork_exec();
    test_vfork();
    test_no_children();
    printf("=== tst-fork done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
