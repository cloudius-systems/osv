/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <iostream>
#include <stdlib.h>

namespace osv {

/**
 * The shutdown is an stub implementation for the osv shutdown.
 * because the header signature of the function uses __attribute__((noreturn))
 * The function cannot return normally and it exit instead
 */
void shutdown(void)
{
    std::cout << "shutdown" << std::endl;
    exit(0);
}

}
