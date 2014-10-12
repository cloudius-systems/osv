/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test how the console behaves when accessed in different ways (i.e., file
// descriptor 0, and /dev/console). For open()ed files, all file operations
// pass through the vnops and/or devops abstractions and the vfs_file class
// which introduce various limitations and bugs which this test tries to
// reproduce (this test can be compiled on both Linux and OSv, to allow
// comparing their behavior).

#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#include <string>
#include <thread>
#include <iostream>
#include <vector>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

#ifdef __OSV__
#define CONSOLE_FILE "/dev/console"
#else
#define CONSOLE_FILE "/dev/tty"
#endif

int main(int ac, char** av)
{
    printf("*** try inputting something here in 3 seconds (followed by newline) ***\n");
    sleep(3); // Give the tester some time to enter some input for the following test.

    // Verify two things.
    // 1. If there was stdin input (followed by newline) during the sleep above,
    //    poll() should show some input was available. Our console_file::poll()
    //    used to always return 0, causing this test to fail.
    // 2. After reading all available input, poll() should say that no further input
    //    is available (shouldn't continue to infinitely show input is available).
    pollfd pfd;
    pfd.fd = 0;
    pfd.events = POLLIN;
    while (poll(&pfd, 1, 0) == 1) {
        // input is available, so we can read one character without blocking.
        char c;
        report(read(0, &c, 1) == 1, "read one available character");
    }
    report(poll(&pfd, 1, 0) == 0, "no more available characters");

    // Verify that poll() with timeout also works.
    printf("*** try inputting something here in 3 seconds (followed by newline) ***\n");
    while (poll(&pfd, 1, 3000) == 1) {
        char c;
        report(read(0, &c, 1) == 1, "read one available character");
    }
    report(poll(&pfd, 1, 0) == 0, "no more available characters");


#if 0
    // TODO: Do same as above, but use the console as an opened /dev/console
    // instead of file descriptor 0. This not only tests the console, but more
    // generally verifies that passing through the VFS layer does not cause
    // problems for poll() (see issue #515).

    // TODO: similarly, also test ioctl on /dev/console. I think it will also
    // not work similarly to issue #515 because the ioctl is not passed to the
    // underlying implementation.

    // TODO: check opening /dev/console with O_NONBLOCK. I think it does
    // not work correctly at the moment (console_multiplexer::read()
    // ignores wither is_blocking(ioflag) and always blocks.
#endif

    // Verify that isatty() is true for file descriptor 0, and also for /dev/console
    report(isatty(0), "fd 0 is a tty");
    int fd = open(CONSOLE_FILE, O_RDONLY);
    report(fd >= 0, "open() " CONSOLE_FILE " for reading");
    report(isatty(fd), "open()ed fd is a tty");
    report(close(fd) == 0, "close opened()ed fd");


    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}


