/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * This file stub functionality found in the OSv main.
 *
 * This allows running the server locally without linkage the osv libraries
 */

#include <string>
#include <sys/utsname.h>

namespace osv {

std::string version()
{
    struct utsname name;
    uname(&name);
    return name.release;
}

}
