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

BOOST_AUTO_TEST_CASE(test_error_codes)
{
    char path[PATH_MAX] = "/tmp/tmpfileXXXXXX";

    mktemp(path);

    auto fd = open(path, O_CREAT | O_RDWR | O_TRUNC);

    BOOST_REQUIRE_MESSAGE(fd > 0, "open a file");

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
