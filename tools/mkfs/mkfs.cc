/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
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

// Created to guarantee that shared objects resources will
// be surely released at the function prologue.
void run_cmd(const char *cmdpath, vector<string> args)
{
    int ret;
    auto ok = run(cmdpath, args, &ret);
    assert(ok && ret == 0);
}

void mkfs()
{
    // Create zfs device, then /etc/mnttab which is required by libzfs
    zfsdev::zfsdev_init();

    // Manually create /etc/mnttab, a file required by libzfs.
    mkdir("/etc", 0755);
    int fd = creat("/etc/mnttab", 0644);
    assert(fd != -1);
    close(fd);

    // Create zpool named osv
    run_cmd("/zpool.so",
        {"zpool", "create", "-f", "-R", "/zfs", "osv", "/dev/vblk0.1"});

    // Create a zfs dataset within the pool named osv.
    run_cmd("/zfs.so", {"zfs", "create", "osv/zfs"});
}

int main(int ac, char** av)
{
    cout << "Running mkfs...\n";
    mkfs();
    sync();
}

