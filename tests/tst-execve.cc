/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test for execve() launching a program in a fresh ELF namespace
 * (see documentation/fork.md).  Covers the two independent facts that the
 * original execve() new-namespace path broke:
 *   1. a successful execve() actually LAUNCHES the target program, and
 *   2. after the exec'd program exits, control returns/reaps cleanly and OSv
 *      is able to shut down (the fork-child app_runtime leak used to hang it).
 * Requires CONF_fork (execve is a stub otherwise).
 */
#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>
#include <cerrno>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main()
{
    printf("=== tst-execve ===\n"); fflush(stdout);

    // execve() a real payload from a fork child; a successful exec never
    // returns, so if it returns the exec FAILED (child exits 99).  The payload
    // (/tests/payload-exit7.so) prints a marker and exit(7)s.
    pid_t pid = fork();
    if (pid == 0) {
        char *const argv[] = { (char*)"/tests/payload-exit7.so", nullptr };
        char *const envp[] = { nullptr };
        execve(argv[0], argv, envp);
        _exit(99);   // execve returned => it failed to launch the program
    }
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    CHECK(w == pid, "waitpid() reaps the exec'd child");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7,
          "execve launched the payload and its exit code (7) was reaped");

    // execve() of a missing path must fail cleanly with ENOENT (not crash).
    char *const bad[] = { (char*)"/tests/does-not-exist.so", nullptr };
    errno = 0;
    int rc = execve(bad[0], bad, nullptr);
    CHECK(rc == -1 && errno == ENOENT,
          "execve of a missing path returns -1/ENOENT");

    printf("=== tst-execve done: %d failures ===\n", failures); fflush(stdout);
    return failures == 0 ? 0 : 1;
}
