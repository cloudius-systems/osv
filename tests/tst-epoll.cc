/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/epoll.h>
#include <sys/poll.h>
#include <unistd.h>
#include "sched.hh"
#include "debug.hh"
#include "drivers/clock.hh"

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int main(int ac, char** av)
{
    int ep = epoll_create(1);
    report(ep >= 0, "epoll_create");

    int s[2];
    int r = pipe(s);
    report(r == 0, "create pipe");

#define MAXEVENTS 1024
    struct epoll_event events[MAXEVENTS];

    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 0, "epoll_wait for empty epoll");

    r = epoll_wait(s[0], events, MAXEVENTS, 0);
    report(r == -1 && errno == EINVAL, "epoll_wait on non-epoll fd");

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.u32 = 123;
    r = epoll_ctl(ep, EPOLL_CTL_ADD, s[0], &event);
    report(r == 0, "epoll_ctl_add");

    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 0, "epoll_wait returns nothing");

    char c = 'N';
    r = write(s[1], &c, 1);
    report(r == 1, "write single character");

    struct pollfd p;
    p.fd = s[0];
    p.events = POLLIN;
    r = poll(&p, 1, 0);
    report(r == 1 && (p.revents & POLLIN), "poll finds fd");

    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 1 && (events[0].events & EPOLLIN) &&
            (events[0].data.u32 == 123), "epoll_wait finds fd");

    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 1 && (events[0].events & EPOLLIN) &&
            (events[0].data.u32 == 123), "epoll_wait again");

    r = read(s[0], &c, 1);
    report(r == 1, "read after poll");

    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 0, "epoll after read");


    sched::thread t1([&] {
        int r2 = epoll_wait(ep, events, MAXEVENTS, 5000);
        report(r2 == 1 && (events[0].events & EPOLLIN) &&
                (events[0].data.u32 == 123), "epoll_wait in thread");

        r2 = read(s[0], &c, 1);
        report(r2 == 1, "read after poll");

        r = epoll_wait(ep, events, MAXEVENTS, 0);
        report(r == 0, "epoll after read");
    });
    t1.start();
    usleep(500000);
    r = write(s[1], &c, 1);
    report(r == 1, "write single character");
    t1.join();

    auto ts = clock::get()->time();
    r = epoll_wait(ep, events, MAXEVENTS, 300);
    auto te = clock::get()->time();
    report(r == 0 && ((te - ts) > 200_ms), "epoll timeout");


    debug("SUMMARY: %d tests, %d failures\n", tests, fails);
}


