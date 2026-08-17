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
    if (ret != 0) {
        std::cerr << "mkfs: command failed (ret=" << ret << "):";
        for (const auto& arg : args)
            std::cerr << " " << arg;
        std::cerr << "\n";
    }
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
static void mkfs(int ac, char** av)
{
    // Create zfs device, then /etc/mnttab which is required by libzfs
    zfsdev_init();

    // Manually create /etc/mnttab, a file required by libzfs.
    mkdir("/etc", 0755);
    int fd = creat("/etc/mnttab", 0644);
    assert(fd != -1);
    close(fd);

    const char *dev_name = ac == 2 ? av[1] : "/dev/vblk0.1";
    // OpenZFS defaults the pool-root mountpoint to /<pool> (i.e. /osv, which
    // under the -R /zfs altroot resolves to /zfs/osv), whereas the old BSD-ZFS
    // port defaulted it to /.  Pin the root mountpoint to / at creation time
    // with -m so the pool root mounts at /zfs and the osv/zfs child inherits
    // /zfs/zfs, matching cpiod's --prefix /zfs/zfs/ and the host-side builder.
    // Setting it via -m (rather than a later 'zfs set mountpoint=') avoids a
    // remount, which the OSv libzfs mount shim cannot perform (dmu_objset_own
    // returns EBUSY for the already-owned root objset).
#ifdef CONF_ZFS_OPENZFS
    vector<string> zpool_args = {"zpool", "create", "-f", "-R", "/zfs", "-m", "/", "osv", dev_name};
#else
    // BSD zpool defaults the root mountpoint to / already and rejects the -m
    // scheme, so omit it.
    vector<string> zpool_args = {"zpool", "create", "-f", "-R", "/zfs", "osv", dev_name};
#endif

    get_blk_devices(zpool_args);

    // Create zpool named osv.  zpool create auto-mounts the root dataset at
    // /zfs.
    run_cmd("/zpool.so", zpool_args);

    // Create the osv/zfs dataset.  It inherits mountpoint /zfs/zfs from the
    // root and is auto-mounted there, so cpiod writes land on real ZFS.
    run_cmd("/zfs.so", {"zfs", "create", "-o", "relatime=on", "osv/zfs"});

    // Both osv and osv/zfs datasets shouldn't be mounted automatically at boot;
    // the loader mounts osv/zfs explicitly via mount_rootfs.  This is set after
    // create so it does not suppress the build-time auto-mount above.
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
    mkfs(ac, av);
    sync();
    return 0;
}

