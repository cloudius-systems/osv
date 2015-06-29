/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/power.hh>
#include <iostream>

// This is a test for OSv's boot not hanging, crashing, etc. The test
// basically reboots after a successful boot - so it tries again and
// again until one boot crashes or hangs (hopefully not). If there is
// no bug, this test will never end.
int main(int argc, char **argv)
{
    std::cout << "Rebooting...\n";
    osv::reboot();
}
