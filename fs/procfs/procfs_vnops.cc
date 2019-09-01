/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>

#include <osv/mount.h>
#include <osv/prex.h>
#include <osv/sched.hh>
#include <osv/mmu.hh>

#include "fs/pseudofs/pseudofs.hh"

#include <libgen.h>
#include <osv/mempool.hh>
#include <osv/printf.hh>

#include <sys/resource.h>
#include <mntent.h>

#include "cpuid.hh"

namespace procfs {

using namespace std;
using namespace pseudofs;

static mutex_t procfs_mutex;
static uint64_t inode_count = 1; /* inode 0 is reserved to root */

static std::string procfs_stats()
{
    int pid = 0, ppid = 0, pgrp = 0, session = 0, tty = 0, tpgid = -1,
        flags = 0;
    // Postpone this one. We need to hook into ZFS statistics to properly figure
    // out which faults are maj, which are min.
    int min_flt = 0, cmin_flt = 0, maj_flt = 0, cmaj_flt = 0;
    unsigned long stime = 0, cutime = 0, cstime = 0;

    using namespace std::chrono;
    unsigned long utime = duration_cast<microseconds>(sched::osv_run_stats()).count();
    char state = 'R';
    int priority = getpriority(PRIO_PROCESS, 0);
    int nice = priority;
    int nlwp = sched::thread::numthreads();
    int start_time = 0; // boot is reference, so it is 0.
    unsigned long rss = memory::stats::total() - memory::stats::free();
    unsigned long vsize = mmu::all_vmas_size();
    unsigned long rss_rlim = memory::stats::total();
    void *start_code = nullptr;
    void *end_code = nullptr;
    void *start_stack = nullptr;
    void *kstk_esp = nullptr;
    void *kstk_eip = nullptr;

    // All the signal stuff is marked in the manpage as obsolete.
    unsigned long pending = 0;
    unsigned long blocked = 0;
    unsigned long sigign = 0;
    unsigned long sigcatch = 0;
    unsigned long nextalarm = 0;
    // Except for this. This is maintained, but we also don't deliver any signal
    unsigned long exit_signal = 0;
    int cpu = sched::cpu::current()->id;
    int wchan = 0, rt_priority = 0, policy = 0; // SCHED_OTHER = 0
    int zero = 0;

    std::ostringstream os;
    osv::fprintf(os, "%d (%s) %c "
                     "%d %d %d %d %d "
                     "%lu %lu %lu %lu %lu "
                     "%lu %lu %ld %ld "
                     "%ld %ld %ld %ld "
                     "%lu %lu "
                     "%lu %lu "
                     "%p %p %p %p %p "
                     "%lu %lx %lu %lu "
                     "%d %d %d "
                     "%lu %d %d %d",
                     pid, program_invocation_short_name, state,
                     ppid, pgrp, session, tty, tpgid,
                     flags, min_flt, cmin_flt, maj_flt, cmaj_flt,
                     utime, stime, cutime, cstime,
                     priority, nice, nlwp, nextalarm,
                     start_time, vsize,
                     rss, rss_rlim,
                     start_code, end_code, start_stack, kstk_esp, kstk_eip,
                     pending, blocked, sigign, sigcatch,
                     wchan, zero, zero,
                     exit_signal, cpu, rt_priority, policy
                );
    return os.str();
}

static std::string procfs_status()
{
    // The /proc/self/status in Linux contains most of the same
    // information /proc/self/stat does but presented to a human user
    // OSv version exposes only bare minimum of it which is NUMA-related
    // information about cpus and memory intended for numa library
    // For details about the format read here: http://man7.org/linux/man-pages/man5/proc.5.html
    // and here: http://man7.org/linux/man-pages/man7/cpuset.7.html (mask and list format)
    auto cpu_count = sched::cpus.size();
    uint32_t first_set = 0xffffffff >> (32 - cpu_count % 32);
    int remaining_cpus_sets = cpu_count / 32;

    std::ostringstream os;
    osv::fprintf(os, "Cpus_allowed:\t%08x", first_set);
    for (; remaining_cpus_sets > 0; remaining_cpus_sets--) {
        osv::fprintf(os, ",%08x", 0xffffffff);
    }
    osv::fprintf(os, "\nCpus_allowed_list:\t0-%d\n"
                       "Mems_allowed:\t00000001\n"
                       "Mems_allowed_list:\t0\n", cpu_count - 1);
    return os.str();
}

static std::string procfs_mounts()
{
	std::string rstr;
	FILE        *fp = setmntent("/proc/mounts", "r");
	if (!fp) {
		return rstr;
	}

	char   *line = NULL;
	size_t lsz   = 0;

	while (getline(&line, &lsz, fp) > 0) {
		rstr += line;
	}

	free(line);

	endmntent(fp);
	return rstr;
}

static std::string procfs_hostname()
{
    char hostname[65];
    int ret = gethostname(hostname, 65);
    if (ret < 0) {
        return std::string("");
    }

    return std::string(hostname);
}

static int
procfs_mount(mount* mp, const char *dev, int flags, const void* data)
{
    auto* vp = mp->m_root->d_vnode;

    auto self = make_shared<pseudo_dir_node>(inode_count++);
    self->add("maps", inode_count++, mmu::procfs_maps);
    self->add("stat", inode_count++, procfs_stats);
    self->add("status", inode_count++, procfs_status);

    auto kernel = make_shared<pseudo_dir_node>(inode_count++);
    kernel->add("hostname", inode_count++, procfs_hostname);

    auto sys = make_shared<pseudo_dir_node>(inode_count++);
    sys->add("kernel", kernel);

    auto* root = new pseudo_dir_node(vp->v_ino);
    root->add("self", self);
    root->add("0", self); // our standard pid
    root->add("mounts", inode_count++, procfs_mounts);
    root->add("sys", sys);

    root->add("cpuinfo", inode_count++, [] { return processor::features_str(); });

    vp->v_data = static_cast<void*>(root);

    return 0;
}

static int
procfs_unmount(mount* mp, int flags)
{
    release_mp_dentries(mp);

    return 0;
}

} // namespace procfs

static int
procfs_readdir(vnode *vp, file *fp, dirent *dir) {
    std::lock_guard <mutex_t> lock(procfs::procfs_mutex);
    return pseudofs::readdir(vp, fp, dir);
}

int procfs_init(void)
{
    return 0;
}

vnops procfs_vnops = {
    pseudofs::open,               // vop_open
    pseudofs::close,              // vop_close
    pseudofs::read,               // vop_read
    pseudofs::write,              // vop_write
    (vnop_seek_t)     vop_nullop, // vop_seek
    pseudofs::ioctl,              // vop_ioctl
    (vnop_fsync_t)    vop_nullop, // vop_fsync
    procfs_readdir,               // vop_readdir
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

vfsops procfs_vfsops = {
    procfs::procfs_mount,         // vfs_mount
    procfs::procfs_unmount,       // vfs_unmount
    (vfsop_sync_t)   vfs_nullop,  // vfs_sync
    (vfsop_vget_t)   vfs_nullop,  // vfs_vget
    (vfsop_statfs_t) vfs_nullop,  // vfs_statfs
    &procfs_vnops,                // vfs_vnops
};
