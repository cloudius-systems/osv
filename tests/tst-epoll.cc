/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/epoll.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>

#include <string>
#include <iostream>
#include <chrono>
#include <thread>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

int main(int ac, char** av)
{
    int ep = epoll_create(1);
    report(ep >= 0, "epoll_create");

    int s[2];
    int r = pipe(s);
    report(r == 0, "create pipe");

    constexpr int MAXEVENTS = 1024;
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
            (events[0].data.u32 == 123), "epoll_wait finds again (because not EPOLLET)");

    r = read(s[0], &c, 1);
    report(r == 1, "read after poll");

    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 0, "epoll after read");


    std::thread t1([&] {
        int r2 = epoll_wait(ep, events, MAXEVENTS, 5000);
        report(r2 == 1 && (events[0].events & EPOLLIN) &&
                (events[0].data.u32 == 123), "epoll_wait in thread");

        r2 = read(s[0], &c, 1);
        report(r2 == 1, "read after poll");

        r = epoll_wait(ep, events, MAXEVENTS, 0);
        report(r == 0, "epoll after read");
    });
    std::this_thread::sleep_for(std::chrono::microseconds(500000));
    r = write(s[1], &c, 1);
    report(r == 1, "write single character");
    t1.join();

    auto ts = std::chrono::high_resolution_clock::now();
    r = epoll_wait(ep, events, MAXEVENTS, 300);
    auto te = std::chrono::high_resolution_clock::now();
    report(r == 0 && ((te - ts) > std::chrono::milliseconds(200)),
            "epoll timeout");

    ////////////////////////////////////////////////////////////////////////////
    // Test EPOLLET (edge-triggered event notification)
    // Also test EPOLL_CTL_MOD at the same time.
    event.events = EPOLLIN | EPOLLET;
    event.data.u32 = 456;
    r = epoll_ctl(ep, EPOLL_CTL_MOD, s[0], &event);
    report(r == 0, "epoll_ctl_mod");
    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 0, "epoll nothing happened yet");
    r = write(s[1], &c, 1);
    report(r == 1, "write single character");
    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 1 && (events[0].events & EPOLLIN) &&
            (events[0].data.u32 == 456), "epoll_wait finds fd");
    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 0, "epoll_wait doesn't find again (because of EPOLLET)");
    // Also verify that epoll_wait with a timeout doesn't return immediately,
    // (despite fp->poll() being true right after poll_install()).
    ts = std::chrono::high_resolution_clock::now();
    r = epoll_wait(ep, events, MAXEVENTS, 300);
    te = std::chrono::high_resolution_clock::now();
    report(r == 0 && ((te - ts) > std::chrono::milliseconds(200)),
            "epoll timeout doesn't return immediately (EPOLLET)");
    // The accurate edge-triggered behavior of EPOLLET means that until the
    // all the data is read from the pipe, epoll should not find the pipe again
    // even if new data comes in. However, both Linux and OSv gives a "false
    // positive" where if new data arrives, epoll will return even if the data
    // was not fully read previously.
    r = write(s[1], &c, 1);
    report(r == 1, "write single character");
    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 1 && (events[0].events & EPOLLIN) &&
            (events[0].data.u32 == 456), "epoll_wait false positive (fine)");
    r = read(s[0], &c, 1);
    report(r == 1, "read one byte out of 2 on the pipe");
    // We only read one byte out of the 2 on the pipe, so there's still data
    // on the pipe (and poll() verifies this), but with EPOLLET, epoll won't
    // return it.
    r = epoll_wait(ep, events, MAXEVENTS, 0);
    report(r == 0, "now epoll_wait doesn't find this fd (EPOLLET)");
    r = poll(&p, 1, 0);
    report(r == 1 && (p.revents & POLLIN), "but poll() does find this fd");
    r = read(s[0], &c, 1);
    report(r == 1, "read the last byte on the pipe");


    std::cout << "SUMMARY: " << tests << ", " << fails << " failures\n";
}


