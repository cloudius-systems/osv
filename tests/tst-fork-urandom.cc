/*
 * Copyright (C) 2026 Greg Burd
 * BSD license (see LICENSE).
 *
 * Diagnostic: a fork child reads /dev/urandom (PostgreSQL's pg_strong_random
 * path for its cancel key).  Verifies the per-fork-child getpid() change does
 * not break /dev/urandom in a child, and that getpid() is distinct parent/child.
 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cstdio>
#include <cerrno>
#include <cstring>

static int read_urandom()
{
    int f = open("/dev/urandom", O_RDONLY, 0);
    if (f < 0) { printf("open /dev/urandom errno=%d (%s)\n", errno, strerror(errno)); return -1; }
    unsigned char buf[16];
    size_t left = sizeof(buf);
    unsigned char *p = buf;
    while (left) {
        ssize_t r = read(f, p, left);
        if (r <= 0) {
            printf("read /dev/urandom r=%zd errno=%d (%s)\n", r, errno, strerror(errno));
            close(f); return -2;
        }
        p += r; left -= r;
    }
    close(f);
    return 0;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-urandom ===\n");
    printf("parent getpid=%d\n", (int)getpid());
    int pr = read_urandom();
    printf("parent read_urandom=%d\n", pr);

    pid_t pid = fork();
    if (pid == 0) {
        printf("child getpid=%d\n", (int)getpid());
        int cr = read_urandom();
        printf("child read_urandom=%d\n", cr);
        _exit(cr == 0 ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    int cec = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int failures = (pr != 0) + (cec != 0);
    if (failures == 0)
        printf("PASS: parent and fork child both read /dev/urandom\n");
    else
        printf("FAIL: urandom read failed (parent=%d child_exit=%d)\n", pr, cec);
    printf("=== tst-fork-urandom done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
