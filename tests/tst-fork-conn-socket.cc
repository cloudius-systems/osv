/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for [W-connsock]: PostgreSQL's postmaster->backend connection
 * handoff.  The postmaster accept()s an inbound connection (a NEW connection
 * socket fd), fork()s a client backend to serve it, and THEN closes the
 * connection fd in the postmaster (postmaster.c: "We no longer need the open
 * socket in this process" -> closesocket(s.sock)).  The forked backend then
 * uses the inherited connection fd -- pq_init() calls getsockname(port->sock).
 *
 * OSv has ONE GLOBAL fd table (fs/vfs/kern_descrip.cc gfdt[]) shared by a fork
 * parent and child.  Before the fix, the postmaster's close() of the connection
 * fd nulled the shared gfdt slot (the postmaster is NOT a fork child, so the
 * 0a874234f child-close guard did not apply).  The forked backend then either
 * saw gfdt[fd]==NULL -> getsockname()=EBADF, or the freed fd was reused by the
 * backend's own startup open() for a regular file -> getsockname()=ENOTSOCK.
 * Stock PG18.4 died with "FATAL: getsockname() failed: ENOTSOCK/EBADF" and the
 * psql connection was closed before any query was served.
 *
 * This test reproduces the handoff directly: parent listens, a connector thread
 * connects, parent accept()s a REAL connection fd, fork()s, the PARENT closes
 * the connection fd (exactly like the postmaster).  The forked CHILD -- the
 * "backend" -- then calls getsockname(), getpeername() and recv() on the
 * inherited connection fd (mirroring pq_init()), which MUST succeed (not
 * ENOTSOCK/EBADF).  The CHILD owns the verdict and prints PASS/FAIL itself,
 * because on OSv it is the forked backend, exactly like a real PG backend, that
 * either serves or dies -- and printing from the child avoids depending on the
 * fork exit-status / waitpid plumbing.  On HEAD (before the fix) the child's
 * getsockname() fails -> "FAIL"; with the fix -> "PASS".
 *
 * To reproduce (in arena-dev, CONF_fork=y build):
 *   ./scripts/build conf_fork=1 fs=rofs image=tests, then boot with -smp 2
 *   (fork needs >=2 vCPUs on this build; -smp 1 hangs fork):
 *   qemu ... --rootfs=rofs /tests/tst-fork-conn-socket.so  (-smp 2)
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <thread>

static unsigned short g_port = 0;

// Connector thread: connect to the parent's listen socket and send one byte so
// the parent has a real connection to accept() and hand off to the child.  It
// then holds the connection open so the child's recv() sees the byte.
static void connector()
{
    int c = socket(AF_INET, SOCK_STREAM, 0);
    if (c < 0) {
        printf("connector: socket() failed errno=%d\n", errno);
        return;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(g_port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (int tries = 0; tries < 200; tries++) {
        if (connect(c, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
            char b = 0x5a;
            (void)write(c, &b, 1);
            usleep(600000);   // keep the connection open for the child's recv()
            close(c);
            return;
        }
        usleep(20000);
    }
    printf("connector: connect() gave up errno=%d\n", errno);
    close(c);
}

int main()
{
    // Unbuffered stdout: on OSv a fork child's exit does not flush the shared
    // stdio buffer, so make every printf visible immediately.
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-conn-socket ===\n");

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        printf("FAIL: socket() errno=%d\n", errno);
        return 1;
    }
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = 0;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        printf("FAIL: bind() errno=%d\n", errno);
        return 1;
    }
    socklen_t slen = sizeof(sa);
    if (getsockname(lfd, (struct sockaddr *)&sa, &slen) < 0) {
        printf("FAIL: getsockname(listen) errno=%d\n", errno);
        return 1;
    }
    g_port = ntohs(sa.sin_port);
    if (listen(lfd, 5) < 0) {
        printf("FAIL: listen() errno=%d\n", errno);
        return 1;
    }
    printf("parent listening on 127.0.0.1:%u (listen fd=%d)\n", g_port, lfd);

    std::thread t(connector);

    // The postmaster's inbound-connection path: accept() a real connection.
    int cfd = accept(lfd, nullptr, nullptr);
    if (cfd < 0) {
        printf("FAIL: accept() errno=%d\n", errno);
        t.detach();
        return 1;
    }
    printf("parent accepted connection fd=%d\n", cfd);

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD = the forked backend serving the connection.  Wait so the
        // parent's close() of the connection fd (below) runs FIRST -- exactly
        // the postmaster->backend race that broke stock PG.
        usleep(150000);

        // Mirror pq_init(): getsockname() on the inherited connection fd.
        struct sockaddr_in la;
        socklen_t ll = sizeof(la);
        if (getsockname(cfd, (struct sockaddr *)&la, &ll) < 0) {
            printf("FAIL: forked backend getsockname(conn fd=%d) errno=%d "
                   "(EBADF=%d ENOTSOCK=%d) -- inherited connection socket lost\n",
                   cfd, errno, EBADF, ENOTSOCK);
            _exit(1);
        }
        struct sockaddr_in ra;
        socklen_t rl = sizeof(ra);
        if (getpeername(cfd, (struct sockaddr *)&ra, &rl) < 0) {
            printf("FAIL: forked backend getpeername(conn fd=%d) errno=%d\n",
                   cfd, errno);
            _exit(1);
        }
        char buf = 0;
        ssize_t n = recv(cfd, &buf, 1, 0);
        if (n != 1 || buf != 0x5a) {
            printf("FAIL: forked backend recv(conn fd=%d) n=%zd buf=0x%02x "
                   "errno=%d\n", cfd, n, (unsigned char)buf, errno);
            _exit(1);
        }
        // The child (backend) is the one that succeeds or dies, exactly like a
        // real PG backend, so it owns the verdict.
        printf("PASS: forked backend used the inherited connection socket "
               "(getsockname family=%d, getpeername family=%d, recv 0x%02x)\n",
               la.sin_family, ra.sin_family, (unsigned char)buf);
        _exit(0);
    }
    if (pid < 0) {
        printf("FAIL: fork() errno=%d\n", errno);
        t.detach();
        return 1;
    }

    // PARENT = postmaster: "We no longer need the open socket in this process."
    // Close the connection fd in the parent.  Before the fix this nulls the
    // shared gfdt slot the child still needs.
    close(cfd);

    // Let the child (backend) run to completion and print the verdict.  The
    // child owns the PASS/FAIL line (it is the backend that succeeds or dies).
    // The detached connector keeps the connection alive for the child's recv().
    t.detach();
    usleep(1500000);   // give the child time to print its verdict
    return 0;
}
