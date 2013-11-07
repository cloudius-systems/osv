/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __NETWORKING_H__
#define __NETWORKING_H__

#include <sys/cdefs.h>
#include <string>
#include <functional>

namespace osv {
    void for_each_if(std::function<void (std::string)> func);
    /* Interface Functions */
    int start_if(std::string if_name, std::string ip_addr,
        std::string mask_addr);
    int ifup(std::string if_name);
}

#endif /* __NETWORKING_H__ */
