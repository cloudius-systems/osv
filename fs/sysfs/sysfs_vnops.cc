/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>
#include <osv/mount.h>
#include <mntent.h>
#include <osv/printf.hh>
#include <osv/mempool.hh>

#include "fs/pseudofs/pseudofs.hh"

namespace sysfs {

using namespace std;
using namespace pseudofs;

static uint64_t inode_count = 1; /* inode 0 is reserved to root */

static mutex_t sysfs_mutex;

static string sysfs_cpumap()
{
    return pseudofs::cpumap() + "\n";
}

static string sysfs_distance()
{
    return std::string("10");
}

using namespace memory;
static string sysfs_free_page_ranges()
{
    stats::page_ranges_stats stats;
    stats::get_page_ranges_stats(stats);

    std::ostringstream os;
    if (stats.order[page_ranges_max_order].ranges_num) {
        osv::fprintf(os, "huge %04d %012ld\n", //TODO: Show in GB/MB/KB
           stats.order[page_ranges_max_order].ranges_num, stats.order[page_ranges_max_order].bytes);
    }

    for (int order = page_ranges_max_order; order--; ) {
        if (stats.order[order].ranges_num) {
            osv::fprintf(os, "  %02d %04d %012ld\n",
               order + 1, stats.order[order].ranges_num, stats.order[order].bytes);
        }
    }

    return os.str();
}

static string sysfs_memory_pools()
{
    stats::pool_stats stats;
    stats::get_global_l2_stats(stats);

    std::ostringstream os;
    osv::fprintf(os, "global l2 (in batches) %02d %02d %02d %02d\n",
        stats._max, stats._watermark_lo, stats._watermark_hi, stats._nr);

    for (auto cpu : sched::cpus) {
        stats::pool_stats stats;
        stats::get_l1_stats(cpu->id, stats);
        osv::fprintf(os, "cpu %d l1 (in pages) %03d %03d %03d %03d\n",
            cpu->id, stats._max, stats._watermark_lo, stats._watermark_hi, stats._nr);
    }

    return os.str();
}

static int
sysfs_mount(mount* mp, const char *dev, int flags, const void* data)
{
    auto* vp = mp->m_root->d_vnode;

    auto node0 = make_shared<pseudo_dir_node>(inode_count++);
    node0->add("meminfo", inode_count++, [] { return pseudofs::meminfo("Node 0 MemTotal:\t%ld kB\nNode 0 MemFree: \t%ld kB\n"); });
    node0->add("cpumap", inode_count++, sysfs_cpumap);
    node0->add("distance", inode_count++, sysfs_distance);

    auto node = make_shared<pseudo_dir_node>(inode_count++);
    node->add("node0", node0);

    auto system = make_shared<pseudo_dir_node>(inode_count++);
    system->add("node", node);

    auto devices = make_shared<pseudo_dir_node>(inode_count++);
    devices->add("system", system);

    auto memory = make_shared<pseudo_dir_node>(inode_count++);
    memory->add("free_page_ranges", inode_count++, sysfs_free_page_ranges);
    memory->add("pools", inode_count++, sysfs_memory_pools);
    memory->add("linear_maps", inode_count++, mmu::sysfs_linear_maps);

    auto osv_extension = make_shared<pseudo_dir_node>(inode_count++);
    osv_extension->add("memory", memory);

    auto* root = new pseudo_dir_node(vp->v_ino);
    root->add("devices", devices);
    root->add("osv", osv_extension);

    vp->v_data = static_cast<void*>(root);

    return 0;
}

static int
sysfs_unmount(mount* mp, int flags)
{
    release_mp_dentries(mp);

    return 0;
}

} // namespace procfs

int sysfs_init(void)
{
    return 0;
}

static int
sysfs_readdir(vnode *vp, file *fp, dirent *dir) {
    std::lock_guard <mutex_t> lock(sysfs::sysfs_mutex);
    return pseudofs::readdir(vp, fp, dir);
}

vnops sysfs_vnops = {
    pseudofs::open,               // vop_open
    pseudofs::close,              // vop_close
    pseudofs::read,               // vop_read
    pseudofs::write,              // vop_write
    (vnop_seek_t)     vop_nullop, // vop_seek
    pseudofs::ioctl,              // vop_ioctl
    (vnop_fsync_t)    vop_nullop, // vop_fsync
    sysfs_readdir,                // vop_readdir
    pseudofs::lookup,             // vop_lookup
    (vnop_create_t)   vop_einval, // vop_create
    (vnop_remove_t)   vop_einval, // vop_remove
    (vnop_rename_t)   vop_einval, // vop_remame
    (vnop_mkdir_t)    vop_einval, // vop_mkdir
    (vnop_rmdir_t)    vop_einval, // vop_rmdir
    pseudofs::getattr,            // vop_getattr
    (vnop_setattr_t)  vop_eperm,  // vop_setattr
    (vnop_inactive_t) vop_nullop, // vop_inactive
    (vnop_truncate_t) vop_nullop, // vop_truncate
    (vnop_link_t)     vop_eperm,  // vop_link
    (vnop_cache_t)     nullptr,   // vop_arc
    (vnop_fallocate_t) vop_nullop, // vop_fallocate
    (vnop_readlink_t)  vop_nullop, // vop_readlink
    (vnop_symlink_t)   vop_nullop, // vop_symlink
};

vfsops sysfs_vfsops = {
    sysfs::sysfs_mount,           // vfs_mount
    sysfs::sysfs_unmount,         // vfs_unmount
    (vfsop_sync_t)   vfs_nullop,  // vfs_sync
    (vfsop_vget_t)   vfs_nullop,  // vfs_vget
    (vfsop_statfs_t) vfs_nullop,  // vfs_statfs
    &sysfs_vnops,                 // vfs_vnops
};
