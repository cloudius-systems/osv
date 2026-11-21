/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A forking server (PostgreSQL's postmaster) accepts a connection, forks a
// child to serve it, and closes its own copy of the connection socket while
// the child runs. Several children are alive at once, and each fork inherits
// every fd open at that moment, including connection sockets that belong to
// other, still-running children.
//
// The parent keeps up to WINDOW children alive, so every child is forked
// while older children still hold their sockets. OSv has one fd table for all
// of them, so the open sockets at any moment are the union of every live
// process's: the listener, the client thread's current socket, and per live
// child its connection socket plus at most one client socket it inherited at
// fork. After every child is reaped, only the listener is left.
//
// waitpid() returns once the child has exited, but OSv's thread reaper drops
// the child's inherited fds a little later, so each count is retaken for up to
// two seconds until it is within its bound. A leaked socket never drains.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <deque>

static const int WINDOW = 8;

static int count_sockets(int cap)
{
    int n = 0;
    for (int fd = 0; fd < cap; fd++) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISSOCK(st.st_mode)) {
            n++;
        }
    }
    return n;
}

// The socket count once it is at most @limit, or after two seconds.
static int settled_sockets(int limit)
{
    int n = count_sockets(65536);
    for (int ms = 0; ms < 2000 && n > limit; ms += 10) {
        usleep(10000);
        n = count_sockets(65536);
    }
    return n;
}

static int port;
static long conns;

static void *client(void *)
{
    for (long i = 0; i < conns; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            printf("FAIL: client socket %ld (errno=%d)\n", i, errno);
            exit(1);
        }
        struct sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
            printf("FAIL: connect %ld (errno=%d)\n", i, errno);
            exit(1);
        }
        char c;
        if (read(s, &c, 1) != 1) {
            printf("FAIL: read reply %ld (errno=%d)\n", i, errno);
            exit(1);
        }
        close(s);
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    conns = argc > 1 ? strtol(argv[1], nullptr, 10) : 1000;
    printf("=== tst-fork-overlap-sockets: %ld connections, up to %d children alive ===\n",
           conns, WINDOW);

    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        printf("SUMMARY: 0 failures\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);

    int l = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t al = sizeof(a);
    if (bind(l, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(l, 128) != 0 ||
            getsockname(l, (struct sockaddr *)&a, &al) != 0) {
        printf("FAIL: listen (errno=%d)\n", errno);
        return 1;
    }
    port = ntohs(a.sin_port);
    int baseline = count_sockets(65536);
    printf("baseline sockets: %d\n", baseline);

    // Each child holds its connection until the parent tells it to finish,
    // through a pipe opened before the fork, so children overlap.
    pthread_t t;
    pthread_create(&t, nullptr, client, nullptr);
    struct child { pid_t pid; int go; };
    std::deque<child> live;
    auto reap_oldest = [&]() {
        child c = live.front();
        live.pop_front();
        char g = 'g';
        if (write(c.go, &g, 1) != 1) {
            printf("FAIL: release child (errno=%d)\n", errno);
            exit(1);
        }
        close(c.go);
        int st = 0;
        waitpid(c.pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            printf("FAIL: child status 0x%x\n", st);
            exit(1);
        }
    };
    const int allowed = baseline + 2 * WINDOW + 1;
    int worst = baseline;
    for (long i = 0; i < conns; i++) {
        int s = accept(l, nullptr, nullptr);
        if (s < 0) {
            printf("FAIL: accept %ld (errno=%d, EMFILE=%d)\n", i, errno, EMFILE);
            return 1;
        }
        int p[2];
        if (pipe(p) != 0) {
            printf("FAIL: pipe %ld (errno=%d)\n", i, errno);
            return 1;
        }
        pid_t pid = fork();
        if (pid == 0) {
            char c = 'x';
            if (write(s, &c, 1) != 1) {
                _exit(2);
            }
            char g;
            if (read(p[0], &g, 1) != 1) {
                _exit(3);
            }
            // Exit with the connection socket still open, as a backend does.
            _exit(0);
        }
        close(p[0]);
        close(s);
        live.push_back({pid, p[1]});
        if ((int)live.size() >= WINDOW) {
            reap_oldest();
        }
        if ((i + 1) % 250 == 0) {
            int n = settled_sockets(allowed);
            if (n > worst) {
                worst = n;
            }
            printf("conn %6ld: sockets = %d (baseline %d)\n", i + 1, n, baseline);
        }
    }
    while (!live.empty()) {
        reap_oldest();
    }
    pthread_join(t, nullptr);

    int final_socks = settled_sockets(baseline);
    printf("final sockets = %d, worst = %d, baseline = %d\n", final_socks, worst, baseline);
    int failures = 0;
    if (worst > allowed) {
        printf("FAIL: %d sockets open with at most %d children alive (allowed %d)\n",
               worst, WINDOW, allowed);
        failures++;
    }
    if (final_socks != baseline) {
        printf("FAIL: %d connection sockets still open after every child was reaped\n",
               final_socks - baseline);
        failures++;
    }
    if (failures) {
        printf("SUMMARY: %d failures\n", failures);
        return 1;
    }
    printf("PASS: no connection socket left open\n");
    printf("SUMMARY: 0 failures\n");
    return 0;
}
