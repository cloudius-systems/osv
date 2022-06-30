/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <assert.h>
#include <string.h>
#include <iostream>

#include <osv/osv_c_wrappers.h>

using namespace std;

static void run_cmd(const char *cmdpath, vector<string> args)
{
    std::vector<const char*> cargs{};

    for(const auto& arg: args)
        cargs.push_back(arg.c_str());

    auto ret = osv_run_app(cmdpath, cargs.data(), cargs.size());
    assert(ret == 0);
}

// Get extra blk devices for pool creation.
static void get_blk_devices(vector<string> &zpool_args)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir("/dev/");
    assert(dir != nullptr);

    while ((entry = readdir(dir)) != nullptr) {
        if (!strstr(entry->d_name, "vblk")) {
            continue;
        }

        // Skip vblk0: partition where kernel text/data lives in.
        // Skip vblk0.1: partition unconditionally used for pool creation.
        if (strstr(entry->d_name, "vblk0")) {
            continue;
        }

        zpool_args.push_back("/dev/" + string(entry->d_name));
    }

    closedir(dir);
}

extern "C" void zfsdev_init();
static void mkfs(void)
{
    // Create zfs device, then /etc/mnttab which is required by libzfs
    zfsdev_init();

    // Manually create /etc/mnttab, a file required by libzfs.
    mkdir("/etc", 0755);
    int fd = creat("/etc/mnttab", 0644);
    assert(fd != -1);
    close(fd);

    vector<string> zpool_args = {"zpool", "create", "-f", "-R", "/zfs", "osv",
        "/dev/vblk0.1"};

    get_blk_devices(zpool_args);

    // Create zpool named osv
    run_cmd("/zpool.so", zpool_args);

    // Create a zfs dataset within the pool named osv.
    run_cmd("/zfs.so", {"zfs", "create", "-o", "relatime=on", "osv/zfs"});

    // Both osv and osv/zfs datasets shouldn't be mounted automatically.
    run_cmd("/zfs.so", {"zfs", "set", "canmount=noauto", "osv"});
    run_cmd("/zfs.so", {"zfs", "set", "canmount=noauto", "osv/zfs"});

    // Enable lz4 compression on the created zfs dataset
    // NOTE: Compression is disabled after image creation.
    run_cmd("/zfs.so", {"zfs", "set", "compression=lz4", "osv"});
}

__attribute__((__visibility__("default")))
int main(int ac, char** av)
{
    cout << "Running mkfs...\n";
    mkfs();
    sync();
    return 0;
}

