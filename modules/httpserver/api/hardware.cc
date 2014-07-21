/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "hardware.hh"
#include "json/formatter.hh"
#include "autogen/hardware.json.hh"
#include "processor.hh"
#include <osv/sched.hh>

namespace httpserver {

namespace api {

namespace hardware {

using namespace std;
using namespace json;
using namespace hardware_json;

void init(routes& routes)
{
    hardware_json_init_path();

    processorFeatures.set_handler([](const_req req)
    {
        return processor::features_str();
    });

    processorCount.set_handler([](const_req req)
    {
        return sched::cpus.size();
    });
}

}
}
}
