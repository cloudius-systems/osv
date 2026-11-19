/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// pthread_self() in a fork child names the child's own thread, so the
// pthread calls that take it work there: set and read the affinity, set and
// read the name, and read the attributes.  50 children, then a fork child
// that forks again does the same.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-pthread-self requires CONF_fork\n");
    return 0;
}

#else

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// 0 if every call on pthread_self() works, else the number of the failing step.
static int check_self()
{
    pthread_t me = pthread_self();
    if (!me) {
        return 1;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (pthread_setaffinity_np(me, sizeof(set), &set)) {
        return 2;
    }
    CPU_ZERO(&set);
    if (pthread_getaffinity_np(me, sizeof(set), &set) || !CPU_ISSET(0, &set)) {
        return 3;
    }
    if (pthread_setname_np(me, "forkchild")) {
        return 4;
    }
    char name[16] = {};
    if (pthread_getname_np(me, name, sizeof(name)) || strcmp(name, "forkchild")) {
        return 5;
    }
    pthread_attr_t a;
    if (pthread_getattr_np(me, &a)) {
        return 6;
    }
    if (!pthread_equal(me, pthread_self())) {
        return 7;
    }
    return 0;
}

static int reap(pid_t c)
{
    int st = 0;
    if (c < 0 || waitpid(c, &st, 0) != c || !WIFEXITED(st)) {
        return 100;
    }
    return WEXITSTATUS(st);
}

int main()
{
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

    int failures = 0;
    for (int i = 0; i < 50; i++) {
        pid_t c = fork();
        if (c == 0) {
            _exit(check_self());
        }
        int r = reap(c);
        if (r) {
            printf("FAIL: child %d: step %d\n", i, r);
            failures++;
            break;
        }
    }
    pid_t c = fork();
    if (c == 0) {
        int r = check_self();
        if (r) {
            _exit(r);
        }
        pid_t g = fork();
        if (g == 0) {
            _exit(check_self());
        }
        int rg = reap(g);
        _exit(rg ? 50 + (rg % 50) : 0);
    }
    int r = reap(c);
    if (r) {
        printf("FAIL: nested: code %d\n", r);
        failures++;
    }
    if (!failures) {
        printf("PASS: tst-fork-pthread-self\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
