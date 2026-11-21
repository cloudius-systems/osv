/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A forked process blocks in a BSD sleep (accept() and a blocking connect()
// both reach msleep()) and is woken by a connection made from another
// process.  The waker walks the sleep queue and marks the sleeper awake from
// its own address space.  If the sleeper's queue entry lives on its stack or
// in its heap arena (private to the forked process's copy-on-write address
// space), the waker reads and writes a different copy: the guest faults in
// the wakeup path, or the sleeper is never woken and a watchdog fails the
// test.
//
// Usage: tst-fork-msleep-wake.so [rounds] (default 200).

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-msleep-wake requires CONF_fork\n");
    return 0;
}

#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <vector>

// What the process was waiting for when the watchdog fired.
static const char *volatile stage = "start";

static void watchdog(int)
{
    static const char msg[] = "FAIL: a sleeper was never woken, waiting in: ";
    write(1, msg, sizeof(msg) - 1);
    write(1, stage, strlen(stage));
    write(1, "\n", 1);
    _exit(1);
}

static int listener(unsigned short *port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(a);
    if (s < 0 || bind(s, (sockaddr*)&a, sizeof(a)) < 0 || listen(s, 16) < 0 ||
        getsockname(s, (sockaddr*)&a, &len) < 0) {
        return -1;
    }
    *port = ntohs(a.sin_port);
    return s;
}

static int connect_to(unsigned short port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    if (s < 0 || connect(s, (sockaddr*)&a, sizeof(a)) < 0) {
        if (s >= 0) {
            close(s);
        }
        return -1;
    }
    return s;
}

// The child dirties its heap first, so its arena pages are its own copies,
// then sleeps in accept() until the parent connects.
static int child_accept(int ls)
{
    std::vector<char*> junk;
    for (int i = 0; i < 64; i++) {
        junk.push_back(static_cast<char*>(malloc(256)));
        memset(junk.back(), i, 256);
    }
    stage = "child accept";
    alarm(15);
    int c = accept(ls, nullptr, nullptr);
    stage = "child read";
    char b = 0;
    int ok = c >= 0 && read(c, &b, 1) == 1 && b == 'x';
    alarm(0);
    for (auto p : junk) {
        free(p);
    }
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? atoi(argv[1]) : 200;
    signal(SIGALRM, watchdog);
    int failures = 0;
    for (int r = 0; r < rounds; r++) {
        unsigned short port;
        int ls = listener(&port);
        if (ls < 0) {
            printf("FAIL: listen\n");
            return 1;
        }
        pid_t pid = fork();
        if (pid == 0) {
            _exit(child_accept(ls));
        }
        if (pid < 0) {
            printf("FAIL: fork\n");
            return 1;
        }
        // Let the child reach accept() before the connection arrives.
        usleep(2000);
        alarm(20);
        stage = "connect";
        int c = connect_to(port);
        if (c < 0 || write(c, "x", 1) != 1) {
            printf("FAIL: round %d: connect\n", r);
            failures++;
        }
        int status = 0;
        stage = "waitpid (the child's accept)";
        if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            printf("FAIL: round %d: child status 0x%x\n", r, status);
            failures++;
        }
        alarm(0);
        if (c >= 0) {
            close(c);
        }
        close(ls);
    }
    printf("%d rounds, %d failures\n", rounds, failures);
    if (failures == 0) {
        printf("PASS\n");
    }
    return failures ? 1 : 0;
}

#endif
