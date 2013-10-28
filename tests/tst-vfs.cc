/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-vfs

#include "sched.hh"
#include "debug.hh"
#include "tst-fs.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>

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
                        BOOST_REQUIRE(stat("/usr/lib/jvm/jre/lib/amd64/headless/libmawt.so", &buf)==0);
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
