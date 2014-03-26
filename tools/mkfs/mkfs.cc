/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <assert.h>
#include <osv/device.h>
#include <osv/run.hh>
#include <fs/vfs/vfs.h>
#include <iostream>
#include "drivers/zfs.hh"

using namespace osv;
using namespace std;

void mkfs()
{
    /* Create zfs device, then /etc/mnttab which is required by libzfs */
    zfsdev::zfsdev_init();
    mkdir("/etc", 0755);
    int fd = creat("/etc/mnttab", 0644);
    assert(fd != -1);
    close(fd);

    int ret;
    auto ok = run("/zpool.so",
            {"zpool", "create", "-f", "-R", "/zfs", "osv", "/dev/vblk0.1"}, &ret);
    assert(ok && ret == 0);
    ok = run("/zfs.so", {"zfs", "create", "osv/zfs"}, &ret);
    assert(ok && ret == 0);
}

int main(int ac, char** av)
{
    cout << "Running mkfs...\n";
    mkfs();
    sync();
}

