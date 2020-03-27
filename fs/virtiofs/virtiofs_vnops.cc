/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/stat.h>
#include <dirent.h>
#include <sys/param.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/debug.h>

#include <sys/types.h>
#include <osv/device.h>
#include <osv/sched.hh>

#include "virtiofs.hh"
#include "virtiofs_i.hh"

#define VERIFY_READ_INPUT_ARGUMENTS() \
    /* Cant read directories */\
    if (vnode->v_type == VDIR) \
        return EISDIR; \
    /* Cant read anything but reg */\
    if (vnode->v_type != VREG) \
        return EINVAL; \
    /* Cant start reading before the first byte */\
    if (uio->uio_offset < 0) \
        return EINVAL; \
    /* Need to read more than 1 byte */\
    if (uio->uio_resid == 0) \
        return 0; \
    /* Cant read after the end of the file */\
    if (uio->uio_offset >= (off_t)vnode->v_size) \
        return 0;

int virtiofs_init(void) {
    return 0;
}

static int virtiofs_lookup(struct vnode *vnode, char *name, struct vnode **vpp)
{
    struct virtiofs_inode *inode = (struct virtiofs_inode *) vnode->v_data;
    struct vnode *vp = nullptr;

    if (*name == '\0') {
        return ENOENT;
    }

    if (!S_ISDIR(inode->attr.mode)) {
        kprintf("[virtiofs] inode:%d, ABORTED lookup of %s because not a directory\n", inode->nodeid, name);
        return ENOTDIR;
    }

    auto *out_args = new (std::nothrow) fuse_entry_out();
    auto input = new char[strlen(name) + 1];
    strcpy(input, name);

    auto *strategy = reinterpret_cast<fuse_strategy*>(vnode->v_mount->m_data);
    int error = fuse_req_send_and_receive_reply(strategy, FUSE_LOOKUP, inode->nodeid,
            input, strlen(name) + 1, out_args, sizeof(*out_args));

    if (!error) {
        if (vget(vnode->v_mount, out_args->nodeid, &vp)) { //TODO: Will it ever work? Revisit
            virtiofs_debug("lookup found vp in cache!\n");
            *vpp = vp;
            return 0;
        }

        auto *new_inode = new virtiofs_inode();
        new_inode->nodeid = out_args->nodeid;
        virtiofs_debug("inode %d, lookup found inode %d for %s!\n", inode->nodeid, new_inode->nodeid, name);
        memcpy(&new_inode->attr, &out_args->attr, sizeof(out_args->attr));

        virtiofs_set_vnode(vp, new_inode);
        *vpp = vp;
    } else {
        kprintf("[virtiofs] inode:%d, lookup failed to find %s\n", inode->nodeid, name);
        //TODO Implement proper error handling by sending FUSE_FORGET
    }

    delete input;
    delete out_args;

    return error;
}

static int virtiofs_open(struct file *fp)
{
    if ((file_flags(fp) & FWRITE)) {
        // Do no allow opening files to write
        return (EROFS);
    }

    struct vnode *vnode = file_dentry(fp)->d_vnode;
    struct virtiofs_inode *inode = (struct virtiofs_inode *) vnode->v_data;

    auto *out_args = new (std::nothrow) fuse_open_out();
    auto *input_args = new (std::nothrow) fuse_open_in();
    input_args->flags = O_RDONLY;

    auto *strategy = reinterpret_cast<fuse_strategy*>(vnode->v_mount->m_data);
    int error = fuse_req_send_and_receive_reply(strategy, FUSE_OPEN, inode->nodeid,
            input_args, sizeof(*input_args), out_args, sizeof(*out_args));

    if (!error) {
        virtiofs_debug("inode %d, opened\n", inode->nodeid);

        auto *file_data = new virtiofs_file_data();
        file_data->file_handle = out_args->fh;
        fp->f_data = file_data;
    }

    delete input_args;
    delete out_args;

    return error;
}

static int virtiofs_close(struct vnode *vnode, struct file *fp)
{
    struct virtiofs_inode *inode = (struct virtiofs_inode *) vnode->v_data;

    auto *input_args = new (std::nothrow) fuse_release_in();
    auto *file_data = reinterpret_cast<virtiofs_file_data*>(fp->f_data);
    input_args->fh = file_data->file_handle;

    auto *strategy = reinterpret_cast<fuse_strategy*>(vnode->v_mount->m_data);
    auto error = fuse_req_send_and_receive_reply(strategy, FUSE_RELEASE, inode->nodeid,
            input_args, sizeof(*input_args), nullptr, 0);

    if (!error) {
        fp->f_data = nullptr;
        delete file_data;
        virtiofs_debug("inode %d, closed\n", inode->nodeid);
    }

    //TODO: Investigate if we should send FUSE_FORGET once all handles to the file closed on our side

    delete input_args;

    return error;
}

static int virtiofs_readlink(struct vnode *vnode, struct uio *uio)
{
    struct virtiofs_inode *inode = (struct virtiofs_inode *) vnode->v_data;

    auto *link_path = new (std::nothrow) char[PATH_MAX];

    auto *strategy = reinterpret_cast<fuse_strategy*>(vnode->v_mount->m_data);
    int error = fuse_req_send_and_receive_reply(strategy, FUSE_READLINK, inode->nodeid,
            nullptr, 0, link_path, PATH_MAX);

    int ret = 0;
    if (!error) {
        virtiofs_debug("inode %d, read symlink [%s]\n", inode->nodeid, link_path);
        ret = uiomove(link_path, strlen(link_path), uio);
    } else {
        kprintf("[virtiofs] Error reading data\n");
        ret = error;
    }

    delete link_path;

    return ret;
}

//TODO: Optimize it to reduce number of exits to host (each fuse_req_send_and_receive_reply())
// by reading eagerly "ahead/around" just like ROFS does and caching it
static int virtiofs_read(struct vnode *vnode, struct file *fp, struct uio *uio, int ioflag)
{
    struct virtiofs_inode *inode = (struct virtiofs_inode *) vnode->v_data;

    VERIFY_READ_INPUT_ARGUMENTS()

    // Total read amount is what they requested, or what is left
    uint64_t read_amt = std::min<uint64_t>(inode->attr.size - uio->uio_offset, uio->uio_resid);
    void *buf = malloc(read_amt);

    auto *input_args = new (std::nothrow) fuse_read_in();
    auto *file_data = reinterpret_cast<virtiofs_file_data*>(fp->f_data);
    input_args->fh = file_data->file_handle;
    input_args->offset = uio->uio_offset;
    input_args->size = read_amt;
    input_args->flags = ioflag;
    input_args->lock_owner = 0;

    virtiofs_debug("inode %d, reading %d bytes at offset %d\n", inode->nodeid, read_amt, uio->uio_offset);

    auto *strategy = reinterpret_cast<fuse_strategy*>(vnode->v_mount->m_data);
    auto error = fuse_req_send_and_receive_reply(strategy, FUSE_READ, inode->nodeid,
            input_args, sizeof(*input_args), buf, read_amt);

    int ret = 0;
    if (!error) {
        ret = uiomove(buf, read_amt, uio);
    } else {
        kprintf("[virtiofs] Error reading data\n");
        ret = error;
    }

    free(buf);
    free(input_args);

    return ret;
}
//
static int virtiofs_readdir(struct vnode *vnode, struct file *fp, struct dirent *dir)
{
    //TODO Implement
    return EPERM;
}

static int virtiofs_getattr(struct vnode *vnode, struct vattr *attr)
{
    struct virtiofs_inode *inode = (struct virtiofs_inode *) vnode->v_data;

    attr->va_mode = 0555; //Is it really correct?

    if (S_ISDIR(inode->attr.mode)) {
        attr->va_type = VDIR;
    } else if (S_ISREG(inode->attr.mode)) {
        attr->va_type = VREG;
    } else if (S_ISLNK(inode->attr.mode)) {
        attr->va_type = VLNK;
    }

    attr->va_nodeid = vnode->v_ino;
    attr->va_size = inode->attr.size;

    return 0;
}

#define virtiofs_write       ((vnop_write_t)vop_erofs)
#define virtiofs_seek        ((vnop_seek_t)vop_nullop)
#define virtiofs_ioctl       ((vnop_ioctl_t)vop_nullop)
#define virtiofs_create      ((vnop_create_t)vop_erofs)
#define virtiofs_remove      ((vnop_remove_t)vop_erofs)
#define virtiofs_rename      ((vnop_rename_t)vop_erofs)
#define virtiofs_mkdir       ((vnop_mkdir_t)vop_erofs)
#define virtiofs_rmdir       ((vnop_rmdir_t)vop_erofs)
#define virtiofs_setattr     ((vnop_setattr_t)vop_erofs)
#define virtiofs_inactive    ((vnop_inactive_t)vop_nullop)
#define virtiofs_truncate    ((vnop_truncate_t)vop_erofs)
#define virtiofs_link        ((vnop_link_t)vop_erofs)
#define virtiofs_arc         ((vnop_cache_t) nullptr)
#define virtiofs_fallocate   ((vnop_fallocate_t)vop_erofs)
#define virtiofs_fsync       ((vnop_fsync_t)vop_nullop)
#define virtiofs_symlink     ((vnop_symlink_t)vop_erofs)

struct vnops virtiofs_vnops = {
    virtiofs_open,      /* open */
    virtiofs_close,     /* close */
    virtiofs_read,      /* read */
    virtiofs_write,     /* write - returns error when called */
    virtiofs_seek,      /* seek */
    virtiofs_ioctl,     /* ioctl */
    virtiofs_fsync,     /* fsync */
    virtiofs_readdir,   /* readdir */
    virtiofs_lookup,    /* lookup */
    virtiofs_create,    /* create - returns error when called */
    virtiofs_remove,    /* remove - returns error when called */
    virtiofs_rename,    /* rename - returns error when called */
    virtiofs_mkdir,     /* mkdir - returns error when called */
    virtiofs_rmdir,     /* rmdir - returns error when called */
    virtiofs_getattr,   /* getattr */
    virtiofs_setattr,   /* setattr - returns error when called */
    virtiofs_inactive,  /* inactive */
    virtiofs_truncate,  /* truncate - returns error when called*/
    virtiofs_link,      /* link - returns error when called*/
    virtiofs_arc,       /* arc */ //TODO: Implement to allow memory re-use when mapping files, investigate using virtio-fs DAX
    virtiofs_fallocate, /* fallocate - returns error when called*/
    virtiofs_readlink,  /* read link */
    virtiofs_symlink    /* symbolic link - returns error when called*/
};
