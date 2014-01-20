/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-dlfcn

#include <dlfcn.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_dlopen_with_null_as_filename)
{
    auto ref = dlopen(NULL, RTLD_NOW);
    BOOST_REQUIRE(ref != NULL);
    BOOST_REQUIRE(dlsym(ref, "open") != NULL);
    BOOST_REQUIRE(dlclose(ref) == 0);
}

BOOST_AUTO_TEST_CASE(test_dlopen_with_empty_file_name)
{
    auto ref = dlopen("", RTLD_NOW);
    BOOST_REQUIRE(ref != NULL);
    BOOST_REQUIRE(dlsym(ref, "open") != NULL);
    BOOST_REQUIRE(dlclose(ref) == 0);
}

template<typename T>
static void *adj_addr(T addr, int adj)
{
    return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(addr) + adj);
}

BOOST_AUTO_TEST_CASE(test_dladdr)
{
    Dl_info info;

    BOOST_REQUIRE(dladdr(adj_addr(vfprintf, -2), &info) != 0);
    BOOST_REQUIRE(std::string("vfprintf") != std::string(info.dli_sname));
    BOOST_REQUIRE(vfprintf != info.dli_saddr);

    BOOST_REQUIRE(dladdr(adj_addr(vfprintf, 0), &info) != 0);
    BOOST_CHECK_EQUAL("vfprintf", info.dli_sname);
    BOOST_CHECK_EQUAL(vfprintf, info.dli_saddr);

    BOOST_REQUIRE(dladdr(adj_addr(vfprintf, 2), &info) != 0);
    BOOST_CHECK_EQUAL("vfprintf", info.dli_sname);
    BOOST_CHECK_EQUAL(vfprintf, info.dli_saddr);

    BOOST_REQUIRE(dladdr(adj_addr(vfprintf, 4), &info) != 0);
    BOOST_CHECK_EQUAL("vfprintf", info.dli_sname);
    BOOST_CHECK_EQUAL(vfprintf, info.dli_saddr);
}
