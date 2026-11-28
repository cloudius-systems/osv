/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Data a fork child writes into a pipe must stay readable after it exits.
// A pipe's queue grows by allocating blocks on the writing thread and frees
// them on the reading one, so with fork the blocks must be memory every
// process maps: the identity heap, not the writer's private heap.
//
// Each round, a new child writes 6000 bytes (more than one queue block) into
// a pipe the parent made before forking, and exits; once the child is reaped
// the parent reads it all back and checks it.  A block in the dead child's
// private heap is not mapped in the parent, so the read faults or returns
// garbage.  Later rounds also make the parent free blocks the previous child
// grew, before a new child grows the queue again.

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const int rounds = 8;
static const int nbytes = 6000;

static char pattern(int round, int i)
{
    return char('a' + (round * 7 + i * 13) % 26);
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

    int fds[2];
    if (pipe(fds)) {
        printf("FAIL: pipe errno=%d\n", errno);
        return 1;
    }
    int failures = 0;
    static char buf[nbytes];
    for (int r = 0; r < rounds && !failures; r++) {
        pid_t c = fork();
        if (c == 0) {
            for (int i = 0; i < nbytes; i++) {
                buf[i] = pattern(r, i);
            }
            // One byte at a time, so the queue grows block by block here.
            for (int i = 0; i < nbytes; i++) {
                if (write(fds[1], &buf[i], 1) != 1) {
                    _exit(2);
                }
            }
            _exit(0);
        }
        int st = 0;
        waitpid(c, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) {
            printf("FAIL: round %d: writer child exit status 0x%x\n", r, st);
            failures++;
            break;
        }
        // waitpid() returns before the reaper has torn the child down.
        sleep(1);
        int got = 0;
        while (got < nbytes) {
            ssize_t n = read(fds[0], buf + got, nbytes - got);
            if (n <= 0) {
                printf("FAIL: round %d: read returned %zd errno=%d\n", r, n, errno);
                failures++;
                break;
            }
            got += n;
        }
        int bad = 0;
        for (int i = 0; i < got; i++) {
            bad += buf[i] != pattern(r, i);
        }
        if (bad) {
            printf("FAIL: round %d: %d of %d bytes written by the exited child are wrong\n",
                   r, bad, got);
            failures++;
        }
    }
    if (!failures) {
        printf("PASS: %d rounds of %d bytes written by a child, read after it exited\n",
               rounds, nbytes);
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
