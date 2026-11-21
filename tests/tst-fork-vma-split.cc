/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Fork children that split a vma (mprotect the middle page of a private
// mapping, munmap the middle of another), then exit.  The reaper disposes
// each child's vmas from AS0, so a vma object the split created must be
// identity memory: one in the child's private fork-arena memory is not mapped
// in AS0, and walking it faults.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 200;
    pid_t probe = fork();
    if (probe < 0) {
        printf("SKIP: fork() not enabled\nSUMMARY: 0 failures\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);
    for (int i = 0; i < n; i++) {
        pid_t p = fork();
        if (p == 0) {
            char *a = (char *)mmap(nullptr, 3 * 4096, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            memset(a, 1, 3 * 4096);
            mprotect(a + 4096, 4096, PROT_READ);    // split into three vmas
            char *b = (char *)mmap(nullptr, 3 * 4096, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            munmap(b + 4096, 4096);                 // split into two vmas
            _exit(0);
        }
        int st = 0;
        if (p < 0 || waitpid(p, &st, 0) != p || !WIFEXITED(st)) {
            printf("FAIL: child %d status 0x%x\n", i, st);
            return 1;
        }
    }
    sleep(2);   // let the reaper dispose the last children
    printf("PASS: %d children split vmas and were reaped\nSUMMARY: 0 failures\n", n);
    return 0;
}
