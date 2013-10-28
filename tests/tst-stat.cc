/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-stat

#include "tst-fs.hh"

#include <sys/types.h>
#include <sys/stat.h>

#include <boost/test/unit_test.hpp>
#include <boost/filesystem/fstream.hpp>

BOOST_AUTO_TEST_CASE(test_mkdir_fails_if_file_exists)
{
    TempDir tmp;
    fs::path file = mkfile(tmp / "file");
    assert_exists(file);

    assert_stat_error(file / "/", ENOTDIR);

    // Once again to check the path which uses dentry from cache.
    assert_stat_error(file / "/", ENOTDIR);

    assert_stat_error(file / "x", ENOTDIR);
}
