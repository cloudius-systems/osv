/*
 * Copyright (C) 2015 Franco Venturi
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>
#include <sys/select.h>

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
    int s[2];
    int r = pipe(s);
    report(r == 0, "create pipe");

    std::thread t1([&] {
        std::this_thread::sleep_for(std::chrono::microseconds(100000));

        char c = 'N';
        int r1 = write(s[1], &c, 1);
        report(r1 == 1, "write single character");
    });

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s[0], &rfds);

    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    r = select(s[0] + 1, &rfds, NULL, NULL, &tv);
    report(r == 1, "select with fd event");

    char c;
    r = read(s[0], &c, 1);
    report(r == 1 && c == 'N', "read");

    report(tv.tv_sec == 0 && (tv.tv_usec > 10000 && tv.tv_usec < 190000),
           "select with fd event timeout");

    t1.join();

    FD_SET(s[0], &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    r = select(s[0] + 1, &rfds, NULL, NULL, &tv);
    report(r == 0, "select without fd event (i.e. timeout)");
    report(tv.tv_sec == 0 && tv.tv_usec == 0, "select without fd event timeout");

    std::thread t2([&] {
        std::this_thread::sleep_for(std::chrono::microseconds(100000));

        char c = 'N';
        int r1 = write(s[1], &c, 1);
        report(r1 == 1, "write single character");
    });

    FD_SET(s[0], &rfds);

    r = select(s[0] + 1, &rfds, NULL, NULL, NULL);
    report(r == 1, "select with fd event and null timeout");

    c = ' ';
    r = read(s[0], &c, 1);
    report(r == 1 && c == 'N', "read");

    t2.join();

    std::cout << "SUMMARY: " << tests << ", " << fails << " failures\n";
    return !!fails;
}
