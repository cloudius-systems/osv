/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-template

#include "template.hh"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_AUTO_TEST_CASE(test_substitute_one)
{
    template_source src("cluster_name={{clustername}}");

    map<string, string> dict = {{"clustername", "cluster-1"}};

    BOOST_CHECK_EQUAL("cluster_name=cluster-1", src.expand(dict));
}

BOOST_AUTO_TEST_CASE(test_substitute_multiple)
{
    template_source src("cluster_name={{clustername}}\ncluster_name={{clustername}}");

    map<string, string> dict = {{"clustername", "cluster-1"}};

    BOOST_CHECK_EQUAL("cluster_name=cluster-1\ncluster_name=cluster-1", src.expand(dict));
}
