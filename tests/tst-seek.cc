/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-seek

#include <boost/test/unit_test.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <climits>
#include "tst-fs.hh"

int make_file()
{
    char path[PATH_MAX] = "/tmp/tmpfileXXXXXX";
    auto fd = mkstemp(path);
    BOOST_REQUIRE_MESSAGE(fd > 0, "open a file");
    return fd;
}

BOOST_AUTO_TEST_CASE(test_error_codes)
{
    auto fd = make_file();

    int ret;

    ret = lseek(fd, ~0ULL, SEEK_SET);
    BOOST_REQUIRE_MESSAGE(ret == -1 && errno == EINVAL, "SEEK_SET: negative offset");

    ret = lseek(fd, ~0ULL, SEEK_CUR);
    BOOST_REQUIRE_MESSAGE(ret == -1 && errno == EINVAL, "SEEK_CUR: negative offset");

    ret = lseek(fd, ~0ULL, SEEK_END);
    BOOST_REQUIRE_MESSAGE(ret == -1 && errno == EINVAL, "SEEK_END: negative offset");

    BOOST_REQUIRE_MESSAGE(lseek(fd, 0, SEEK_SET) == 0, "SEEK_SET: zero");

    BOOST_REQUIRE_MESSAGE(lseek(fd, 0, SEEK_CUR) == 0, "SEEK_CUR: zero");

    BOOST_REQUIRE_MESSAGE(lseek(fd, 0, SEEK_END) == 0, "SEEK_END: zero");

    BOOST_REQUIRE_MESSAGE(close(fd) != -1, "close the file");
}

#define assert_reads_next(fd, c) \
    do {                                        \
        char buf[1];                            \
        BOOST_REQUIRE(read(fd, buf, 1) == 1);   \
        BOOST_REQUIRE_EQUAL(c, buf[0]);         \
    } while (0)

BOOST_AUTO_TEST_CASE(test_seek_offsets)
{
    auto fd = make_file();

    BOOST_REQUIRE_EQUAL(write(fd, "12345678", 8), 8);

    lseek(fd, 0, SEEK_SET);

    assert_reads_next(fd, '1');
    assert_reads_next(fd, '2');
    assert_reads_next(fd, '3');

    lseek(fd, 0, SEEK_SET);

    assert_reads_next(fd, '1');
    assert_reads_next(fd, '2');

    lseek(fd, 3, SEEK_CUR);

    assert_reads_next(fd, '6');
    assert_reads_next(fd, '7');
    assert_reads_next(fd, '8');

    lseek(fd, -5, SEEK_CUR);

    assert_reads_next(fd, '4');
    assert_reads_next(fd, '5');

    lseek(fd, -5, SEEK_END);

    assert_reads_next(fd, '4');
    assert_reads_next(fd, '5');

    lseek(fd, -1, SEEK_CUR);

    assert_reads_next(fd, '5');
    assert_reads_next(fd, '6');

    BOOST_REQUIRE(close(fd) == 0);
}
