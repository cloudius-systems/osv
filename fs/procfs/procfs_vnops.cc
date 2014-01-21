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
#include "mmu.hh"

#include <functional>
#include <memory>
#include <map>

namespace procfs {

using namespace std;

class proc_node {
public:
    proc_node(uint64_t ino) : _ino(ino) { }
    virtual ~proc_node() { }

    uint64_t ino() const { return _ino; };

    virtual off_t    size() const = 0;
    virtual int      type() const = 0;
    virtual mode_t   mode() const = 0;
private:
    uint64_t _ino;
};

class proc_file_node : public proc_node {
public:
    proc_file_node(uint64_t ino, function<string ()> gen)
        : proc_node(ino)
        , _gen(gen)
    { }

    virtual off_t size() const override {
        return 0;
    }
    virtual int type() const override {
        return VREG;
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
    proc_dir_node(uint64_t ino) : proc_node(ino) { }

    shared_ptr<proc_node> lookup(string name) {
        auto it = _children.find(name);
        if (it == _children.end()) {
            return nullptr;
        }
        return it->second;
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
    virtual int type() const override {
        return VDIR;
    }
    virtual mode_t mode() const override {
        return S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
    }
private:
    map<string, shared_ptr<proc_node>> _children;
};

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
    return ENOENT;
}

static int
procfs_mount(mount* mp, char *dev, int flags, void* data)
{
    auto* vp = mp->m_root->d_vnode;

    auto self = make_shared<proc_dir_node>(inode_count++);
    self->add("maps", inode_count++, mmu::procfs_maps);

    auto* root = new proc_dir_node(vp->v_ino);
    root->add("self", self);

    vp->v_data = static_cast<void*>(root);

    return 0;
}

static int
procfs_unmount(mount* mp, int flags)
{
    release_mp_dentries(mp);

    auto* vp = to_node(mp->m_root->d_vnode);

    delete vp;

    return 0;
}

} // namespace procfs

extern "C"
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
    (vnop_getattr_t)  vop_nullop, // vop_getattr
    (vnop_setattr_t)  vop_eperm,  // vop_setattr
    (vnop_inactive_t) vop_nullop, // vop_inactive
    (vnop_truncate_t) vop_nullop, // vop_truncate
    (vnop_link_t)     vop_eperm,  // vop_link
};

vfsops procfs_vfsops = {
    procfs::procfs_mount,         // vfs_mount
    procfs::procfs_unmount,       // vfs_unmount
    (vfsop_sync_t)   vfs_nullop,  // vfs_sync
    (vfsop_vget_t)   vfs_nullop,  // vfs_vget
    (vfsop_statfs_t) vfs_nullop,  // vfs_statfs
    &procfs_vnops,                // vfs_vnops
};
