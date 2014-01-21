/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-vfs

#include <osv/sched.hh>
#include <osv/debug.hh>
#include "tst-fs.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>

#include <osv/dentry.h>

BOOST_AUTO_TEST_CASE(test_path_lookup)
{
    TempDir dir;

    BOOST_REQUIRE(fs::create_directories(dir / "sub"));

    fs::path valid_paths[] = {
        dir,
        dir / ".",
        dir / "/",
        dir / "/sub",
        dir / "/sub/",
        dir / "/sub/",
        dir / "/sub/.",
        dir / "/sub/..",
        dir / "/sub/../",
        dir / "/sub/../sub",
        dir / "/sub/../sub/",
        dir / "/sub/../sub/.",
        dir / "/sub/../sub/..",
        dir / "/sub/../sub/../",
        dir / "/sub/../sub/../sub"
    };

    for (auto path : valid_paths) {
        assert_exists(path);
    }
}

extern "C" {
    int namei(char *, struct dentry **);
    void drele(struct dentry *);
}

BOOST_AUTO_TEST_CASE(test_dentry_hierarchy)
{
    debug("Running dentry hierarchy tests\n");

    char path[] = "/tests/tst-vfs.so";
    struct dentry *dp;

    BOOST_REQUIRE(!namei(path, &dp));

    // Check if dentry hierarchy is exactly as expected.
    BOOST_CHECK_EQUAL("/tests/tst-vfs.so", dp->d_path);
    BOOST_REQUIRE(dp->d_parent);
    BOOST_CHECK_EQUAL("/tests", dp->d_parent->d_path);
    BOOST_REQUIRE(dp->d_parent->d_parent);
    BOOST_CHECK_EQUAL("/", dp->d_parent->d_parent->d_path);

    drele(dp);

    debug("dentry hierarchy tests succeeded\n");
}

BOOST_AUTO_TEST_CASE(test_concurrent_file_operations)
{
    debug("Running concurrent file operation tests\n");

    constexpr int N = 10;
    debug("test1, with %d threads\n", N);
    sched::thread *threads[N];
    for (int i = 0; i < N; i++) {
            threads[i] = new sched::thread([] {
                    struct stat buf;
                    for (int j = 0; j < 1000; j++) {
                        BOOST_REQUIRE(stat("/tests/tst-vfs.so", &buf)==0);
                    }
            });
    }
    for (int i=0; i<N; i++) {
        threads[i]->start();
    }
    for (int i=0; i<N; i++) {
        threads[i]->join();
        delete threads[i];
    }

    debug("concurrent file operation tests succeeded\n");
}
