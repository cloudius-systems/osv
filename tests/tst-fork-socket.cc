/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for [W-socket]: a fork child that close()s a socket fd it
 * inherited from the parent must only drop the CHILD's reference -- it must NOT
 * tear down the underlying socket/inpcb while the PARENT still holds the fd and
 * is using it.
 *
 * OSv has ONE GLOBAL file-descriptor table (fs/vfs/kern_descrip.cc gfdt[]), so
 * a fork parent and child genuinely share socket fds; fork() does not dup the
 * fd table.  Before the fix, the child's close() ran the full
 * fdclose()->fdrop()->soclose()->in_pcbdetach()/in_pcbfree() teardown on the
 * SHARED socket, so the parent's still-open listen socket (and its inpcb) were
 * destroyed out from under it.  This showed up in stock PostgreSQL as:
 *   Assertion failed: inp->inp_socket == 0L
 *     (bsd/sys/netinet/in_pcb.cc: in_pcbrele_locked:1197)
 * fired when the postmaster's forked child ran ClosePostmasterPorts() and
 * closed the listen socket it inherited.
 *
 * This test reproduces the wall directly: the parent opens a listening TCP
 * socket, fork()s, the CHILD closes its inherited listen fd and exits, then the
 * PARENT accept()s a real loopback connection on that same fd and reads a byte.
 * On HEAD (before the fix) the child's close tears the socket down and the
 * parent's accept() fails / the inpcb assert fires; with the fix the parent's
 * accept() succeeds.
 *
 * To reproduce (in arena-dev, CONF_fork=y build):
 *   ./scripts/build conf_fork=1 fs=rofs image=tests, then:
 *   ./scripts/run.py -c 1 -e /tests/tst-fork-socket.so
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

// Parent's listen fd, shared with the connector thread.
static int g_listen_fd = -1;
static unsigned short g_port = 0;

// A separate thread that connects to the parent's listen socket AFTER the child
// has closed its inherited copy -- so the accept() below is served by the still
// alive parent socket, proving the child's close did not tear it down.
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
    for (int tries = 0; tries < 100; tries++) {
        if (connect(c, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
            char b = 0x5a;
            (void)write(c, &b, 1);
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
    printf("=== tst-fork-socket ===\n");

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
    sa.sin_port = 0;   // let the stack pick a free port
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        printf("FAIL: bind() errno=%d\n", errno);
        return 1;
    }
    socklen_t slen = sizeof(sa);
    if (getsockname(lfd, (struct sockaddr *)&sa, &slen) < 0) {
        printf("FAIL: getsockname() errno=%d\n", errno);
        return 1;
    }
    g_port = ntohs(sa.sin_port);
    if (listen(lfd, 5) < 0) {
        printf("FAIL: listen() errno=%d\n", errno);
        return 1;
    }
    g_listen_fd = lfd;
    printf("parent listening on 127.0.0.1:%u (fd=%d)\n", g_port, lfd);

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD: mimic PostgreSQL's ClosePostmasterPorts() -- close the
        // inherited listen socket, then exit.  This must NOT destroy the
        // socket the parent is about to accept() on.
        close(lfd);
        _exit(0);
    }
    if (pid < 0) {
        printf("FAIL: fork() errno=%d\n", errno);
        return 1;
    }

    // Reap the child so its address-space teardown (which drops its inherited
    // fd refs) has definitely run before the parent uses the socket.
    int st = 0;
    waitpid(pid, &st, 0);
    printf("child reaped (status=0x%x); parent now using the shared socket\n", st);

    // The parent's listen socket must still be alive.  Drive a real connection
    // through it and accept() it -- this is what fails on the buggy build.
    std::thread t(connector);

    int afd = accept(lfd, nullptr, nullptr);
    if (afd < 0) {
        printf("FAIL: parent accept() errno=%d -- child's close tore down the "
               "shared socket\n", errno);
        t.join();
        return 1;
    }
    char buf = 0;
    ssize_t n = read(afd, &buf, 1);
    printf("parent accepted fd=%d, read %zd byte(s) (0x%02x)\n", afd, n,
           (unsigned char)buf);
    close(afd);
    close(lfd);
    t.join();

    if (n == 1 && buf == 0x5a) {
        printf("PASS: child's close only dropped a ref; parent socket survived\n");
        return 0;
    }
    printf("FAIL: parent socket did not serve the connection (n=%zd)\n", n);
    return 1;
}
