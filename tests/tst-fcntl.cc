/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Tests for various fcntl() issues behave as they do on Linux.
//
// To compile on Linux, use: c++ -g -pthread -std=c++11 tst-fcntl.cc


#include <string>
#include <iostream>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

int main(int ac, char** av)
{
    int fd = open("/tmp/tst-fcntl", O_CREAT | O_RDWR, 0777);
    report(fd > 0, "open");

    // GETFL on a default opened file should return the O_RDWR, plus
    // 0100000 (O_LARGEFILE, although this macro doesn't work).
    // The latter is not required by POSIX, but is returned on 64-bit
    // Linux, and allowed by POSIX (as explained in the Linux Standard
    // Base documentation for fcntl.
    int r = fcntl(fd, F_GETFL);
    report((r & O_ACCMODE) == O_RDWR, "GETFL access mode");
    report((r & ~O_ACCMODE) == 0100000, "GETFL rest");
    int save_r = r;

    // Set with F_SETFL the O_NONBLOCK mode
    r = fcntl(fd, F_SETFL, save_r | O_NONBLOCK);
    report(r == 0, "F_SETFL O_NONBLOCK");
    r = fcntl(fd, F_GETFL);
    report(r == (save_r | O_NONBLOCK), "F_GETFL");
    save_r = r;

    // GETFD on a default opened file should return 0
    r = fcntl(fd, F_GETFD);
    report(r == 0, "GETFD default");


    // SETFD with close on exec
    r = fcntl(fd, F_SETFD, FD_CLOEXEC);
    report(r == 0, "SETFD FD_CLOEXEC");

    // GETFD should now return FD_CLOEXEC
    r = fcntl(fd, F_GETFD);
    report(r == FD_CLOEXEC, "GETFD");

    // GETFL should be unchanged by this setting of SETFD
    r = fcntl(fd, F_GETFL);
    report(r == save_r, "GETFL");

    // dup() should have the same GETFL, but different GETFD
    // (back to the default)
    int dupfd = dup(fd);
    r = fcntl(dupfd, F_GETFL);
    report(r == save_r, "GETFL dup");
#if 0
    // Unfortunately, the following test does not pass on OSv, because it
    // currently makes FD_CLOEXEC per open file, instead of per-fd.
    r = fcntl(dupfd, F_GETFD);
    report(r == 0, "GETFD dup");
#endif
    close(dupfd);

    // O_CLOEXEC is *not* a file bit. We shouldn't be able to set
    // it with F_SETFL (Linux just ignores it), but when we get the
    // flags with F_GETFL, we won't see this bogus bit.
    r = fcntl(fd, F_SETFL, save_r | O_CLOEXEC);
    report(r == 0, "SETFL bogus");
    r = fcntl(fd, F_GETFL);
    report(r == save_r, "GETFL bogus");

    close(fd);
    remove("/tmp/tst-fcntl");

    report(fcntl(fd, F_GETFL) == -1 && errno == EBADF, "fcntl on closed fd");


    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails;
}


