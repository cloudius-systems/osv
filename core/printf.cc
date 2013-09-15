/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/printf.hh>

namespace osv {

template <>
std::ostream& fprintf(std::ostream& os, boost::format& fmt)
{
    return os << fmt;
}

}
