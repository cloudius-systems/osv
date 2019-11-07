/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "pseudofs.hh"
#include <osv/sched.hh>

namespace pseudofs {

static pseudo_node *to_node(vnode *vp) {
    return static_cast<pseudo_node *>(vp->v_data);
}

static pseudo_dir_node *to_dir_node(vnode *vp) {
    auto *np = to_node(vp);

    return dynamic_cast<pseudo_dir_node *>(np);
}

static pseudo_file_node *to_file_node(vnode *vp) {
    auto *np = to_node(vp);

    return dynamic_cast<pseudo_file_node *>(np);
}

static pseudo_symlink_node *to_symlink_node(vnode *vp) {
    auto *np = to_node(vp);

    return dynamic_cast<pseudo_symlink_node *>(np);
}

int open(file *fp) {
    auto *np = to_file_node(fp->f_dentry->d_vnode);
    if (np) {
        fp->f_data = np->data();
    }
    return 0;
}

int close(vnode *vp, file *fp) {
    auto *data = static_cast<string *>(fp->f_data);

    delete data;

    return 0;
}

int read(vnode *vp, file *fp, uio *uio, int ioflags) {
    auto *data = static_cast<string *>(fp->f_data);

    if (vp->v_type == VDIR)
        return EISDIR;
    if (vp->v_type != VREG)
        return EINVAL;
    if (uio->uio_offset < 0)
        return EINVAL;
    if (uio->uio_offset >= (off_t) data->size())
        return 0;

    size_t len;

    if ((off_t) data->size() - uio->uio_offset < uio->uio_resid)
        len = data->size() - uio->uio_offset;
    else
        len = uio->uio_resid;

    return uiomove(const_cast<char *>(data->data()) + uio->uio_offset, len, uio);
}

int readlink(vnode *vp, uio *uio) {
    if (vp->v_type != VLNK)
        return EINVAL;

    auto *np = to_symlink_node(vp);
    auto *target_path = np->target_path();
    if (uio->uio_offset >= (off_t) target_path->size())
        return 0;

    return uiomove(const_cast<char *>(target_path->data()) + uio->uio_offset, target_path->size(), uio);
}

int write(vnode *vp, uio *uio, int ioflags) {
    return EINVAL;
}

int ioctl(vnode *vp, file *fp, u_long cmd, void *arg) {
    return EINVAL;
}

int lookup(vnode *dvp, char *name, vnode **vpp) {
    auto *parent = to_dir_node(dvp);

    *vpp = nullptr;

    if (!*name || !parent) {
        return ENOENT;
    }
    auto node = parent->lookup(name);
    if (!node) {
        return ENOENT;
    }
    vnode *vp;
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

int readdir(vnode *vp, file *fp, dirent *dir) {
    pseudo_dir_node *dnp;

    if (fp->f_offset == 0) {
        dir->d_type = DT_DIR;
        if (vfs_dname_copy((char *) &dir->d_name, ".", sizeof(dir->d_name))) {
            return EINVAL;
        }
    } else if (fp->f_offset == 1) {
        dir->d_type = DT_DIR;
        if (vfs_dname_copy((char *) &dir->d_name, "..", sizeof(dir->d_name))) {
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
        } else if (np->type() == VLNK) {
            dir->d_type = DT_LNK;
        } else {
            dir->d_type = DT_REG;
        }

        if (vfs_dname_copy((char *) &dir->d_name, dir_entry->first.c_str(),
                           sizeof(dir->d_name))) {
            return EINVAL;
        }
    }
    dir->d_fileno = fp->f_offset;

    fp->f_offset++;

    return 0;
}

int getattr(vnode *vp, vattr *attr)
{
    attr->va_nodeid = vp->v_ino;
    attr->va_size = vp->v_size;
    return 0;
}

string cpumap()
{
    auto cpu_count = sched::cpus.size();
    uint32_t first_set = 0xffffffff >> (32 - cpu_count % 32);
    int remaining_cpus_sets = cpu_count / 32;

    std::ostringstream os;
    osv::fprintf(os, "%08x", first_set);
    for (; remaining_cpus_sets > 0; remaining_cpus_sets--) {
        osv::fprintf(os, ",%08x", 0xffffffff);
    }
    return os.str();
}
}
