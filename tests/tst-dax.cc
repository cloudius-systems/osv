/*
 * Copyright (C) 2021 Fotis Xenakis
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-dax

#include <boost/test/unit_test.hpp>

#include <fs/virtiofs/virtiofs_dax.hh>

using virtiofs::dax_manager_stub;
using virtiofs::dax_window_stub;

static constexpr uint64_t nodeid = 1;
static constexpr uint64_t file_handle = 1;

// A stub DAX window with a 10-chunk capacity used in all test cases.
static constexpr u64 window_chunks = 10;
static constexpr u64 window_len = \
    window_chunks * dax_manager_stub::DEFAULT_CHUNK_SIZE;
static auto window = dax_window_stub(window_len);

// A dax_manager with a stub underlying DAX window. Used as a test case fixture.
class dax_manager_test : public dax_manager_stub {
public:
    dax_manager_test()
        : dax_manager_stub {window} {}
};

// Tests on an empty window
BOOST_FIXTURE_TEST_SUITE(empty_window_tests, dax_manager_test)

    BOOST_AUTO_TEST_CASE(first_empty_empty)
    {
        BOOST_TEST(first_empty() == 0);
    }

    BOOST_AUTO_TEST_CASE(map_empty)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(map(nodeid, file_handle, 3, 0, mp, false) == 0);
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 3);
        BOOST_TEST(_mappings.size() == 1);
    }

    BOOST_AUTO_TEST_CASE(unmap_empty)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(unmap(1, mp) != 0);
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 0);
        BOOST_TEST(_mappings.empty());
    }

    BOOST_AUTO_TEST_CASE(map_multiple_no_coalesce)
    {
        mapping_part mp {0, 0};

        BOOST_TEST_REQUIRE(map(nodeid, file_handle, 3, 0, mp, false) == 0);

        BOOST_TEST(map(nodeid, file_handle, 2, 5, mp, false) == 0);
        BOOST_TEST(mp.mstart == 3);
        BOOST_TEST(mp.nchunks == 2);
        BOOST_TEST(_mappings.size() == 2);
    }

    BOOST_AUTO_TEST_CASE(find_absent)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(!find(nodeid + 1, 0, mp));
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 0);
        BOOST_TEST(!find(nodeid, 10, mp));
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 0);
    }

BOOST_AUTO_TEST_SUITE_END()

// A pre-populated dax_manager_test. Used as a test case fixture.
class dax_manager_test_populated : public dax_manager_test {
public:
    void setup() {
        mapping_part mp {0, 0};

        BOOST_TEST_REQUIRE(map(nodeid, file_handle, 3, 0, mp, false) == 0);
        BOOST_TEST_REQUIRE(map(nodeid, file_handle, 2, 5, mp, false) == 0);
        // At this point the window's state is:
        // | f[0] | f[1] | f[2] | f[5] | f[6] | empty | empty |...
        // |     mapping #1     |  mapping #2 |
    }
};

// Tests on a pre-populated window
BOOST_FIXTURE_TEST_SUITE(populated_window_tests, dax_manager_test_populated)

    BOOST_AUTO_TEST_CASE(first_empty_populated)
    {
        BOOST_TEST(first_empty() == 5);
    }

    BOOST_AUTO_TEST_CASE(map_coalesce)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(map(nodeid, file_handle, 3, 7, mp, false) == 0);
        BOOST_TEST(mp.mstart == 5);
        BOOST_TEST(mp.nchunks == 3);
        BOOST_TEST(_mappings.size() == 2);
    }

    BOOST_AUTO_TEST_CASE(map_stable)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(map(nodeid, file_handle, 0, 0, mp, false) == 0);
        BOOST_TEST(mp.mstart == 5);
        BOOST_TEST(mp.nchunks == 0);
        BOOST_TEST(_mappings.size() == 2);
    }

    BOOST_AUTO_TEST_CASE(unmap_stable)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(unmap(0, mp) == 0);
        BOOST_TEST(mp.mstart == 5);
        BOOST_TEST(mp.nchunks == 0);
        BOOST_TEST(_mappings.size() == 2);
    }

    BOOST_AUTO_TEST_CASE(unmap_one_whole)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(unmap(2, mp) == 0);
        BOOST_TEST(mp.mstart == 3);
        BOOST_TEST(mp.nchunks == 2);
        BOOST_TEST(_mappings.size() == 1);
    }

    BOOST_AUTO_TEST_CASE(unmap_one_part)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(unmap(1, mp) == 0);
        BOOST_TEST(mp.mstart == 4);
        BOOST_TEST(mp.nchunks == 1);
        BOOST_TEST(_mappings.size() == 2);
    }

    BOOST_AUTO_TEST_CASE(unmap_multiple_part)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(unmap(3, mp) == 0);
        BOOST_TEST(mp.mstart == 2);
        BOOST_TEST(mp.nchunks == 3);
        BOOST_TEST(_mappings.size() == 1);
    }

    BOOST_AUTO_TEST_CASE(unmap_all)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(unmap(5, mp) == 0);
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 5);
        BOOST_TEST(_mappings.empty());
    }

    BOOST_AUTO_TEST_CASE(unmap_excess)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(unmap(6, mp) == 0);
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 5);
        BOOST_TEST(_mappings.empty());
    }

    BOOST_AUTO_TEST_CASE(map_evict_part)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(map(nodeid, file_handle, 6, 0, mp, true) == 0);
        BOOST_TEST(mp.mstart == 4);
        BOOST_TEST(mp.nchunks == 6);
        BOOST_TEST(_mappings.size() == 3);
    }

    BOOST_AUTO_TEST_CASE(map_evict_multiple_part)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(map(nodeid, file_handle, 9, 0, mp, true) == 0);
        BOOST_TEST(mp.mstart == 1);
        BOOST_TEST(mp.nchunks == 9);
        BOOST_TEST(_mappings.size() == 2);
    }

    BOOST_AUTO_TEST_CASE(map_evict_all)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(map(nodeid, file_handle, 10, 0, mp, true) == 0);
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 10);
        BOOST_TEST(_mappings.size() == 1);
    }

    BOOST_AUTO_TEST_CASE(find_absent)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(!find(nodeid + 1, 0, mp));
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 0);
        BOOST_TEST(!find(nodeid, 10, mp));
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 0);
    }

    BOOST_AUTO_TEST_CASE(find_present)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(find(nodeid, 0, mp));
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 3);

        BOOST_TEST(find(nodeid, 1, mp));
        BOOST_TEST(mp.mstart == 1);
        BOOST_TEST(mp.nchunks == 2);

        BOOST_TEST(find(nodeid, 5, mp));
        BOOST_TEST(mp.mstart == 3);
        BOOST_TEST(mp.nchunks == 2);

        BOOST_TEST(find(nodeid, 6, mp));
        BOOST_TEST(mp.mstart == 4);
        BOOST_TEST(mp.nchunks == 1);
    }

BOOST_AUTO_TEST_SUITE_END()

// A full dax_manager_test. Used as a test case fixture.
class dax_manager_test_full : public dax_manager_test {
public:
    void setup() {
        mapping_part mp {0, 0};

        BOOST_TEST_REQUIRE(map(nodeid, file_handle, 3, 0, mp, false) == 0);
        BOOST_TEST_REQUIRE(map(nodeid, file_handle, 2, 5, mp, false) == 0);
        BOOST_TEST_REQUIRE(map(nodeid, file_handle, 5, 10, mp, false) == 0);
        // At this point the window's state is:
        // | f[0] | f[1] | f[2] | f[5] | f[6] | f[10] | f[11] | f[12] | f[13] | f[14] |
        // |     mapping #1     |  mapping #2 |               mapping #3              |
    }
};

BOOST_FIXTURE_TEST_SUITE(full_window_tests, dax_manager_test_full)

    BOOST_AUTO_TEST_CASE(first_empty_full)
    {
        BOOST_TEST(first_empty() == window_chunks);
    }

    BOOST_AUTO_TEST_CASE(map_full)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(map(nodeid, file_handle, 1, 10, mp, false) != 0);
        BOOST_TEST(mp.mstart == 10);
        BOOST_TEST(mp.nchunks == 0);
        BOOST_TEST(_mappings.size() == 3);
    }

    BOOST_AUTO_TEST_CASE(map_evict_full)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(map(nodeid, file_handle, 1, 0, mp, true) == 0);
        BOOST_TEST(mp.mstart == 9);
        BOOST_TEST(mp.nchunks == 1);
        BOOST_TEST(_mappings.size() == 4);
    }

    BOOST_AUTO_TEST_CASE(find_absent)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(!find(nodeid + 1, 0, mp));
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 0);
        BOOST_TEST(!find(nodeid, 20, mp));
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 0);
    }

    BOOST_AUTO_TEST_CASE(find_present)
    {
        mapping_part mp {0, 0};

        BOOST_TEST(find(nodeid, 0, mp));
        BOOST_TEST(mp.mstart == 0);
        BOOST_TEST(mp.nchunks == 3);

        BOOST_TEST(find(nodeid, 1, mp));
        BOOST_TEST(mp.mstart == 1);
        BOOST_TEST(mp.nchunks == 2);

        BOOST_TEST(find(nodeid, 5, mp));
        BOOST_TEST(mp.mstart == 3);
        BOOST_TEST(mp.nchunks == 2);

        BOOST_TEST(find(nodeid, 6, mp));
        BOOST_TEST(mp.mstart == 4);
        BOOST_TEST(mp.nchunks == 1);

        BOOST_TEST(find(nodeid, 11, mp));
        BOOST_TEST(mp.mstart == 6);
        BOOST_TEST(mp.nchunks == 4);
    }

BOOST_AUTO_TEST_SUITE_END()
