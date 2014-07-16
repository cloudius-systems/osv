/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <climits>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

int main(int argc, char* argv[])
{
    char path[PATH_MAX] = "/tmp/tmpfileXXXXXX";

    mktemp(path);

    auto fd = open(path, O_CREAT | O_RDWR | O_TRUNC);

    report(fd > 0, "open a file");

    int ret;

    ret = lseek(fd, ~0ULL, SEEK_SET);
    report(ret == -1 && errno == EINVAL, "SEEK_SET: negative offset");

    ret = lseek(fd, ~0ULL, SEEK_CUR);
    report(ret == -1 && errno == EINVAL, "SEEK_CUR: negative offset");

    ret = lseek(fd, ~0ULL, SEEK_END);
    report(ret == -1 && errno == EINVAL, "SEEK_END: negative offset");

    report(lseek(fd, 0, SEEK_SET) == 0, "SEEK_SET: zero");

    report(lseek(fd, 0, SEEK_CUR) == 0, "SEEK_CUR: zero");

    report(lseek(fd, 0, SEEK_END) == 0, "SEEK_END: zero");

    report(close(fd) != -1, "close the file");

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}
