/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>
#include <osv/sched.hh>
#include <osv/debug.hh>

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int main(int ac, char** av)
{
    int s[2];

    int r = socketpair(AF_LOCAL, SOCK_STREAM, 0, s);
    report(r == 0, "socketpair call");

    char msg[] = "hello", reply[] = "wrong";
    r = write(s[0], msg, 5);
    report(r == 5, "write to empty socket");
    r = read(s[1], reply, 5);
    report(r == 5 && memcmp(msg, reply, 5) == 0, "read after write");

    memcpy(msg, "snafu", 5);
    memset(reply, 0, 5);
    int r2;
    sched::thread t1([&] {
        r2 = read(s[1], reply, 5);
    });
    t1.start();
    sleep(1);
    r = write(s[0], msg, 5);
    t1.join();
    report(r2 == 5 && memcmp(msg, reply, 5) == 0, "read before write");

    memcpy(msg, "fooba", 5);
    memset(reply, 0, 5);
    pollfd poller = { s[1], POLLIN, 0 };
    r = poll(&poller, 1, 0);
    report(r == 0, "poll() (immediate, no events)");
    r = poll(&poller, 1, 1);
    report(r == 0, "poll() (timeout, no events)");
    r2 = write(s[0], msg, 5);
    r = poll(&poller, 1, 0);
    report(r2 == 5 && r == 1 && poller.revents == POLLIN, "poll() (immediate, has event)");
    r2 = read(s[1], reply, 5);
    report(r2 == 5 && memcmp(msg, reply, 5) == 0, "read after poll");

    memcpy(msg, "smeg!", 5);
    memset(reply, 0, 5);
    sched::thread t2([&] {
        poller.revents = 0;
        r2 = poll(&poller, 1, 5000);
        report(r2 == 1 && poller.revents == POLLIN, "waiting poll");
        r2 = read(s[1], reply, 5);
        report(r2 == 5 && memcmp(msg, reply, 5) == 0, "read after waiting poll");
    });
    t2.start();
    sleep(1);
    r = write(s[0], msg, 5);
    t2.join();
    report(r == 5, "write to polling socket");

    close(s[1]);

    r = write(s[0], msg, 5);
    report(r == -1 && errno == EPIPE, "write, other side closed");

    r = read(s[0], msg, 5);
    report(r == 0, "read, other side closed");

    close(s[0]);

    // Test shutdown() a bit

    r = socketpair(AF_LOCAL, SOCK_STREAM, 0, s);
    report(r == 0, "socketpair call");
    r = shutdown(s[0], SHUT_RD);
    report(r == 0, "shutdown SHUT_RD");
    r = read(s[0], reply, 5);
    report(r == -1 && errno == EBADF, "can't read from SHUT_RD socket");
    r = write(s[1], msg, 5);
    report(r == -1 && errno == EPIPE, "write to socket with other side SHUT_RD");
    r = write(s[0], msg, 5);
    report(r == 5, "SHUT_RD socket can still be written");
    r = read(s[1], reply, 5);
    report(r == 5 && !memcmp(msg, reply, 5), "read from socket with other side SHUT_RD");
    r = close(s[0]);
    report(r == 0, "close after SHUT_RD");
    r = close(s[1]);
    report(r == 0, "close when other end is SHUT_RD");

    r = socketpair(AF_LOCAL, SOCK_STREAM, 0, s);
    report(r == 0, "socketpair call");
    r = shutdown(s[0], SHUT_WR);
    report(r == 0, "shutdown SHUT_WR");
    r = write(s[0], msg, 5);
    report(r == -1 && errno == EBADF, "can't write to SHUT_WR socket");
    r = read(s[1], msg, 5);
    report(r == 0, "read from socket with other side SHUT_WR");
    r = write(s[1], msg, 5);
    report(r == 5, "write to socket with other side SHUT_WR");
    r = read(s[0], reply, 5);
    report(r == 5 && !memcmp(msg, reply, 5), "SHUT_WR socket can still be read");
    r = close(s[0]);
    report(r == 0, "close after SHUT_WR");
    r = close(s[1]);
    report(r == 0, "close when other end is SHUT_WR");


    std::vector<int> sockets;
    while (socketpair(AF_LOCAL, SOCK_STREAM, 0, s) == 0) {
        sockets.push_back(s[0]);
        sockets.push_back(s[1]);
    }
    auto nsock = sockets.size();
    for (int s : sockets) {
        close(s);
    }
    report(nsock > 100, "create many sockets");


    debug("SUMMARY: %d tests, %d failures\n", tests, fails);
}


