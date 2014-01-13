/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/version.hh>

#include <osv/version.h> // For the OSV_VERSION macro

namespace osv {

std::string version()
{
    return OSV_VERSION;
}

}
