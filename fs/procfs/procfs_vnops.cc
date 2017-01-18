/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/dentry.h>
#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/prex.h>
#include <osv/sched.hh>
#include <osv/mmu.hh>

#include <functional>
#include <memory>
#include <map>
#include "fs/vfs/vfs.h"

#include <libgen.h>
#include <osv/mempool.hh>
#include <osv/printf.hh>

#include <sys/resource.h>
#include <mntent.h>

#include "cpuid.hh"

namespace procfs {

using namespace std;

class proc_node {
public:
    proc_node(uint64_t ino, int type) : _ino(ino), _type(type) { }
    virtual ~proc_node() { }

    typedef map<string, shared_ptr<proc_node>> nmap;

    uint64_t ino() const { return _ino; };
    int type() const { return _type; };

    virtual off_t    size() const = 0;
    virtual mode_t   mode() const = 0;
private:
    uint64_t _ino;
    int _type;
};

class proc_file_node : public proc_node {
public:
    proc_file_node(uint64_t ino, function<string ()> gen)
        : proc_node(ino, VREG)
        , _gen(gen)
    { }

    virtual off_t size() const override {
        return 0;
    }
    virtual mode_t mode() const override {
        return S_IRUSR|S_IRGRP|S_IROTH;
    }
    string* data() const {
        return new string(_gen());
    }
private:
    function<string ()> _gen;
};

class proc_dir_node : public proc_node {
public:
    proc_dir_node(uint64_t ino) : proc_node(ino, VDIR) { }

    shared_ptr<proc_node> lookup(string name) {
        auto it = _children.find(name);
        if (it == _children.end()) {
            return nullptr;
        }
        return it->second;
    }
    proc_node::nmap::iterator dir_entries_begin() {
        return _children.begin();
    }
    proc_node::nmap::iterator dir_entries_end() {
        return _children.end();
    }
    bool is_empty() {
        return _children.empty();
    }
    void add(string name, uint64_t ino, function<string ()> gen) {
        _children.insert({name, make_shared<proc_file_node>(ino, gen)});
    }
    void add(string name, shared_ptr<proc_node> np) {
        _children.insert({name, np});
    }
    virtual off_t size() const override {
        return 0;
    }
    virtual mode_t mode() const override {
        return S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
    }
private:
    proc_node::nmap _children;
};

static mutex_t procfs_mutex;
static uint64_t inode_count = 1; /* inode 0 is reserved to root */

static proc_node* to_node(vnode* vp)
{
    return static_cast<proc_node*>(vp->v_data);
}

static proc_dir_node* to_dir_node(vnode* vp)
{
    auto *np = to_node(vp);

    return dynamic_cast<proc_dir_node*>(np);
}

static proc_file_node* to_file_node(vnode* vp)
{
    auto *np = to_node(vp);

    return dynamic_cast<proc_file_node*>(np);
}

static int
procfs_open(file* fp)
{
    auto* np = to_file_node(fp->f_dentry->d_vnode);
    if (np) {
        fp->f_data = np->data();
    }
    return 0;
}

static int
procfs_close(vnode* vp, file* fp)
{
    auto* data = static_cast<string*>(fp->f_data);

    delete data;

    return 0;
}

static int
procfs_read(vnode* vp, file *fp, uio* uio, int ioflags)
{
    auto* data = static_cast<string*>(fp->f_data);

    if (vp->v_type == VDIR)
        return EISDIR;
    if (vp->v_type != VREG)
        return EINVAL;
    if (uio->uio_offset < 0)
        return EINVAL;
    if (uio->uio_offset >= (off_t)data->size())
        return 0;

    size_t len;

    if ((off_t)data->size() - uio->uio_offset < uio->uio_resid)
        len = data->size() - uio->uio_offset;
    else
        len = uio->uio_resid;

    return uiomove(const_cast<char*>(data->data()) + uio->uio_offset, len, uio);
}

static int
procfs_write(vnode* vp, uio* uio, int ioflags)
{
    return EINVAL;
}

static int
procfs_ioctl(vnode* vp, file* fp, u_long cmd, void *arg)
{
    return EINVAL;
}

static int
procfs_lookup(vnode* dvp, char* name, vnode** vpp)
{
    auto* parent = to_dir_node(dvp);

    *vpp = nullptr;

    if (!*name || !parent) {
        return ENOENT;
    }
    auto node = parent->lookup(name);
    if (!node) {
        return ENOENT;
    }
    vnode* vp;
    if (vget(dvp->v_mount, node->ino(), &vp)) {
        /* found in cache */
        *vpp = vp;
        return 0;
    }
    if (!vp) {
        return ENOMEM;
    }
    vp->v_data = node.get();
    vp->v_type = node->type();
    vp->v_mode = node->mode();
    vp->v_size = node->size();

    *vpp = vp;

    return 0;
}

static int
procfs_readdir(vnode *vp, file *fp, dirent *dir)
{
    proc_dir_node *dnp;
    std::lock_guard<mutex_t> lock(procfs_mutex);

    if (fp->f_offset == 0) {
        dir->d_type = DT_DIR;
        if (vfs_dname_copy((char *)&dir->d_name, ".", sizeof(dir->d_name))) {
            return EINVAL;
        }
    } else if (fp->f_offset == 1) {
        dir->d_type = DT_DIR;
        if (vfs_dname_copy((char *)&dir->d_name, "..", sizeof(dir->d_name))) {
            return EINVAL;
        }
    } else {
        dnp = to_dir_node(vp);
        if (dnp->is_empty()) {
            return ENOENT;
        }

        auto dir_entry = dnp->dir_entries_begin();
        for (int i = 0; i != (fp->f_offset - 2) &&
            dir_entry != dnp->dir_entries_end(); i++) {
            dir_entry++;
        }
        if (dir_entry == dnp->dir_entries_end()) {
            return ENOENT;
        }

        auto np = dir_entry->second;
        if (np->type() == VDIR) {
            dir->d_type = DT_DIR;
        } else {
            dir->d_type = DT_REG;
        }

        if (vfs_dname_copy((char *)&dir->d_name, dir_entry->first.c_str(),
                sizeof(dir->d_name))) {
            return EINVAL;
        }
    }
    dir->d_fileno = fp->f_offset;

    fp->f_offset++;

    return 0;
}

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

static int
procfs_mount(mount* mp, const char *dev, int flags, const void* data)
{
    auto* vp = mp->m_root->d_vnode;

    auto self = make_shared<proc_dir_node>(inode_count++);
    self->add("maps", inode_count++, mmu::procfs_maps);
    self->add("stat", inode_count++, procfs_stats);

    auto* root = new proc_dir_node(vp->v_ino);
    root->add("self", self);
    root->add("0", self); // our standard pid
    root->add("mounts", inode_count++, procfs_mounts);
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

static int
procfs_getattr(vnode *vp, vattr *attr)
{
    attr->va_nodeid = vp->v_ino;
    attr->va_size = vp->v_size;
    return 0;
}

} // namespace procfs

int procfs_init(void)
{
    return 0;
}

vnops procfs_vnops = {
    procfs::procfs_open,          // vop_open
    procfs::procfs_close,         // vop_close
    procfs::procfs_read,          // vop_read
    procfs::procfs_write,         // vop_write
    (vnop_seek_t)     vop_nullop, // vop_seek
    procfs::procfs_ioctl,         // vop_ioctl
    (vnop_fsync_t)    vop_nullop, // vop_fsync
    procfs::procfs_readdir,       // vop_readdir
    procfs::procfs_lookup,        // vop_lookup
    (vnop_create_t)   vop_einval, // vop_create
    (vnop_remove_t)   vop_einval, // vop_remove
    (vnop_rename_t)   vop_einval, // vop_remame
    (vnop_mkdir_t)    vop_einval, // vop_mkdir
    (vnop_rmdir_t)    vop_einval, // vop_rmdir
    procfs::procfs_getattr,       // vop_getattr
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
