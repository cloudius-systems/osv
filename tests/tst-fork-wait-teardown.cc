/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// When waitpid() reports that a fork child exited, the child's resources must
// already be released, as on Linux.  Here a child keeps the write end of a
// pipe, which it inherited, and also opens fds of its own, then exits without
// closing anything.  Right after waitpid() returns, the parent (which closed
// its own write end) must read EOF from the pipe, and every fd the child
// opened must be closed.

#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>

#include <osv/kernel_config_fork.h>

#if !CONF_fork
int main()
{
    printf("tst-fork-wait-teardown: CONF_fork disabled, not applicable; SKIP\n");
    return 0;
}
#else

static constexpr int rounds = 50;
static constexpr int child_fds = 16;

static int count_open_fds(int cap)
{
    int n = 0;
    for (int fd = 0; fd < cap; fd++) {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
            n++;
        }
    }
    return n;
}

int main()
{
    int not_eof = 0, fd_residue = 0;
    int baseline = count_open_fds(512);
    for (int r = 0; r < rounds; r++) {
        int p[2];
        if (pipe(p) != 0) {
            printf("FAIL: pipe: errno %d\n", errno);
            return 1;
        }
        pid_t c = fork();
        if (c == 0) {
            close(p[0]);
            for (int i = 0; i < child_fds; i++) {
                open("/dev/null", O_RDONLY);
            }
            _exit(7);
        }
        close(p[1]);
        int st = 0;
        if (waitpid(c, &st, 0) != c || !WIFEXITED(st) || WEXITSTATUS(st) != 7) {
            printf("FAIL: waitpid status 0x%x\n", st);
            return 1;
        }
        fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
        char b;
        ssize_t n = read(p[0], &b, 1);
        if (n != 0) {
            not_eof++;
        }
        close(p[0]);
        int now = count_open_fds(512);
        if (now != baseline) {
            fd_residue++;
        }
    }
    int failures = 0;
    bool ok = not_eof == 0;
    printf("%s: the pipe reads EOF right after waitpid (%d of %d rounds did not)\n",
           ok ? "PASS" : "FAIL", not_eof, rounds);
    failures += !ok;
    ok = fd_residue == 0;
    printf("%s: the child's own fds are closed right after waitpid (%d of %d rounds left some)\n",
           ok ? "PASS" : "FAIL", fd_residue, rounds);
    failures += !ok;
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
#endif
