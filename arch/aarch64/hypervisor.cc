/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/hypervisor.hh>

namespace osv {

hypervisor_type hypervisor()
{
    return hypervisor_type::unknown;
}

}
