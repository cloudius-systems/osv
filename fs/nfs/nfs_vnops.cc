/*
 * Copyright (C) 2015 Scylla, Ltd.
 *
 * Based on nfs code Copyright (c) 2006-2007, Kohsuke Ohtani
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/file.h>
#include <osv/mount.h>

#include "nfs.hh"

static inline struct nfs_context *get_nfs_context(struct vnode *node,
                                                  int &err_no)
{
    return get_mount_context(node->v_mount, err_no)->nfs();
}

static inline struct nfsfh *get_handle(struct vnode *node)
{
    return static_cast<struct nfsfh *>(node->v_data);
}

static inline struct nfsdir *get_dir_handle(struct vnode *node)
{
    return static_cast<struct nfsdir *>(node->v_data);
}


static const char *get_node_name(struct vnode *node)
{
    if (LIST_EMPTY(&node->v_names) == 1) {
        return nullptr;
    }

    return LIST_FIRST(&node->v_names)->d_path;
}

static inline std::string mkpath(struct vnode *node, const char *name)
{
    std::string path(get_node_name(node));
    return path + "/" + name;
}

int nfs_op_open(struct file *fp)
{
    struct vnode *vp = file_dentry(fp)->d_vnode;
    std::string path(fp->f_dentry->d_path);
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    int flags = file_flags(fp);
    int ret = 0;

    if (err_no) {
        return err_no;
    }

    int type = vp->v_type;

    // It's a directory or a file.
    if (type == VDIR) {
        struct nfsdir *handle = nullptr;
        ret = nfs_opendir(nfs, path.c_str(), &handle);
        vp->v_data = handle;
    } else if (type == VREG) {
        struct nfsfh *handle = nullptr;
        ret = nfs_open(nfs, path.c_str(), flags, &handle);
        vp->v_data = handle;
    } else {
        return EIO;
    }

    if (ret) {
        return -ret;
    }

    return 0;
}

int nfs_op_close(struct vnode *dvp, struct file *file)
{
    int err_no;
    auto nfs = get_nfs_context(dvp, err_no);
    int type = dvp->v_type;
    int ret = 0;

    if (err_no) {
        return err_no;
    }

    if (type == VDIR) {
        auto handle = get_dir_handle(dvp);
        nfs_closedir(nfs, handle);
    } else if (type == VREG) {
        auto handle = get_handle(dvp);
        ret = nfs_close(nfs, handle);
    } else {
        return EIO;
    }

    return -ret;
}

static int nfs_op_read(struct vnode *vp, struct file *fp, struct uio *uio,
                    int ioflag)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    auto handle = get_handle(vp);

    if (err_no) {
        return err_no;
    }

    if (vp->v_type == VDIR) {
        return EISDIR;
    }
    if (vp->v_type != VREG) {
        return EINVAL;
    }
    if (uio->uio_offset < 0) {
        return EINVAL;
    }
    if (uio->uio_resid == 0) {
        return 0;
    }

    if (uio->uio_offset >= (off_t)vp->v_size) {
        return 0;
    }

    size_t len;
    if (vp->v_size - uio->uio_offset < uio->uio_resid)
        len = vp->v_size - uio->uio_offset;
    else
        len = uio->uio_resid;

    // FIXME: remove this temporary buffer
    auto buf = std::unique_ptr<char>(new char[len + 1]());

    int ret = nfs_pread(nfs, handle, uio->uio_offset, len, buf.get());
    if (ret < 0) {
        return -ret;
    }

    return uiomove(buf.get(), ret, uio);
}

static int nfs_op_write(struct vnode *vp, struct uio *uio, int ioflag)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    auto handle = get_handle(vp);

    if (err_no) {
        return err_no;
    }

    if (vp->v_type == VDIR) {
        return EISDIR;
    }
    if (vp->v_type != VREG) {
        return EINVAL;
    }
    if (uio->uio_offset < 0) {
        return EINVAL;
    }
    if (uio->uio_resid == 0) {
        return 0;
    }

    int ret = 0;
    size_t new_size = vp->v_size;
    if (ioflag & IO_APPEND) {

        uio->uio_offset = vp->v_size;
        new_size = vp->v_size + uio->uio_resid;
        // NFS does not support appending to a file so let's truncate ourselve
        ret = nfs_ftruncate(nfs, handle, new_size);
    } else if ((uio->uio_offset + uio->uio_resid) > vp->v_size) {
        new_size = uio->uio_offset + uio->uio_resid;
        ret = nfs_ftruncate(nfs, handle, new_size);
    }

    if (ret) {
        return -ret;
    }

    // make a copy of these since uimove will touch them.
    size_t size = uio->uio_resid;
    size_t offset = uio->uio_offset;

    auto buf = std::unique_ptr<char>(new char[size]());

    auto buffp = buf.get();

    ret = uiomove(buffp, size, uio);
    assert(!ret);

    while (size > 0) {
        ret = nfs_pwrite(nfs, handle, offset, size, buffp);
        if (ret < 0) {
            return -ret;
        }

        buffp += ret;
        offset += ret;
        size -= ret;
    }

    vp->v_size = new_size;

    return 0;
}

static int nfs_op_fsync(vnode *vp, struct file *fp)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    auto handle = get_handle(vp);

    if (err_no) {
        return err_no;
    }


    return -nfs_fsync(nfs, handle);
}

static int nfs_op_readdir(struct vnode *vp, struct file *fp, struct dirent *dir)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    auto handle = get_dir_handle(vp);

    if (err_no) {
        return err_no;
    }

    // query the NFS server about this directory entry.
    auto nfsdirent = nfs_readdir(nfs, handle);

    // We finished iterating on the directory.
    if (!nfsdirent) {
        return ENOENT;
    }

    // Fill dirent infos
    assert(sizeof(ino_t) == sizeof(nfsdirent->inode));
    dir->d_ino = nfsdirent->inode;
    // FIXME: not filling dir->d_off
    // FIXME: not filling dir->d_reclen
    dir->d_type = IFTODT(nfsdirent->mode & S_IFMT);
    strlcpy((char *) &dir->d_name, nfsdirent->name, sizeof(dir->d_name));

    // iterate
    fp->f_offset++;

    return 0;
}

// This function is called by the namei() family before nfsen()
static int nfs_op_lookup(struct vnode *dvp, char *p, struct vnode **vpp)
{
    int err_no;
    auto nfs = get_nfs_context(dvp, err_no);
    struct nfs_stat_64 st;
    std::string path = mkpath(dvp, p);
    struct vnode *vp;

    if (err_no) {
        return err_no;
    }

    // Make sure we don't accidentally return garbage.
    *vpp = nullptr;

    // Following 4 checks inspired by ZFS code
    if (!path.size())
        return ENOENT;

    if (dvp->v_type != VDIR)
        return ENOTDIR;

    assert(path != ".");
    assert(path != "..");

    // We must get the inode number so we query the NFS server.
    int ret = nfs_stat64(nfs, path.c_str(), &st);
    if (ret) {
        return -ret;
    }

    // Get the file type.
    uint64_t type = st.nfs_mode & S_IFMT;

    // Filter by inode type: only keep files, directories and symbolic links.
    if (S_ISCHR(type) || S_ISBLK(type) || S_ISFIFO(type) || S_ISSOCK(type)) {
        // FIXME: Not sure it's the right error code.
        return EINVAL;
    }

    // Create the new vnode or get it from the cache.
    if (vget(dvp->v_mount, st.nfs_ino, &vp)) {
        // Present in the cache
        *vpp = vp;
        return 0;
    }

    if (!vp) {
        return ENOMEM;
    }

    uint64_t mode = st.nfs_mode & ~S_IFMT;

    // Fill in the new vnode informations.
    vp->v_type = IFTOVT(type);
    vp->v_mode = mode;
    vp->v_size = st.nfs_size;
    vp->v_mount = dvp->v_mount;

    *vpp = vp;

    return 0;
}

static int nfs_op_create(struct vnode *dvp, char *name, mode_t mode)
{
    int err_no;
    auto nfs = get_nfs_context(dvp, err_no);
    struct nfsfh *handle = nullptr;
    auto path = mkpath(dvp, name);


    if (err_no) {
        return err_no;
    }


    if (!S_ISREG(mode)) {
        return EINVAL;
    }

    int ret = nfs_creat(nfs, path.c_str(), mode, &handle);
    if (ret) {
        return -ret;
    }

    dvp->v_data = handle;
    return 0;
}

static int nfs_op_remove(struct vnode *dvp, struct vnode *vp, char *name)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    auto path = mkpath(dvp, name);

    if (err_no) {
        return err_no;
    }

    return -nfs_unlink(nfs, path.c_str());
}

static int nfs_op_rename(struct vnode *dvp1, struct vnode *vp1, char *old_path,
                      struct vnode *dvp2, struct vnode *vp2, char *new_path)
{
    int err_no;
    auto nfs = get_nfs_context(dvp1, err_no);
    auto src = mkpath(dvp1, old_path);
    auto dst = mkpath(dvp2, new_path);

    if (err_no) {
        return err_no;
    }

    return -nfs_rename(nfs, src.c_str(), dst.c_str());
}

// FIXME: Set permissions
static int nfs_op_mkdir(struct vnode *dvp, char *name, mode_t mode)
{
    int err_no;
    auto nfs = get_nfs_context(dvp, err_no);
    auto path = mkpath(dvp, name);

    if (err_no) {
        return err_no;
    }

    return -nfs_mkdir(nfs, path.c_str());
}

static int nfs_op_rmdir(struct vnode *dvp, struct vnode *vp, char *name)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    auto path = mkpath(dvp, name);

    if (err_no) {
        return err_no;
    }

    return -nfs_rmdir(nfs, path.c_str());
}


static inline struct timespec to_timespec(uint64_t sec, uint64_t nsec)
{
    struct timespec t;

    t.tv_sec = sec;
    t.tv_nsec = nsec;

    return t;
}

static int nfs_op_getattr(struct vnode *vp, struct vattr *attr)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    struct nfs_stat_64 st;

    if (err_no) {
        return err_no;
    }

    auto path = get_node_name(vp);
    if (!path) {
        return ENOENT;
    }

    // Get the file infos.
    int ret = nfs_stat64(nfs, path, &st);
    if (ret) {
        return -ret;
    }

    uint64_t type = st.nfs_mode & S_IFMT;
    uint64_t mode = st.nfs_mode & ~S_IFMT;

    // Copy the file infos.
    //attr->va_mask    =;
    attr->va_type    = IFTOVT(type);
    attr->va_mode    = mode;;
    attr->va_nlink   = st.nfs_nlink;
    attr->va_uid     = st.nfs_uid;
    attr->va_gid     = st.nfs_gid;
    attr->va_fsid    = st.nfs_dev; // FIXME: not sure about this one
    attr->va_nodeid  = st.nfs_ino;
    attr->va_atime   = to_timespec(st.nfs_atime, st.nfs_atime_nsec);
    attr->va_mtime   = to_timespec(st.nfs_mtime, st.nfs_mtime_nsec);
    attr->va_ctime   = to_timespec(st.nfs_ctime, st.nfs_ctime_nsec);
    attr->va_rdev    = st.nfs_rdev;
    attr->va_nblocks = st.nfs_blocks;
    attr->va_size    = st.nfs_size;

    return 0;
}

static int nfs_op_setattr(struct vnode *vp, struct vattr *attr)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    int ret = 0;

    if (err_no) {
        return err_no;
    }

    auto path = get_node_name(vp);
    if (!path) {
        return ENOENT;
    }

    // Change all that we can change with libnfs.

    ret = nfs_chmod(nfs, path, attr->va_mode);
    if (ret) {
        return -ret;
    }

    ret = nfs_chown(nfs, path, attr->va_uid, attr->va_gid);
    if (ret) {
        return -ret;
    }

    return 0;
}

static int nfs_op_truncate(struct vnode *vp, off_t length)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    auto handle = get_handle(vp);

    if (err_no) {
        return err_no;
    }

    int ret = nfs_ftruncate(nfs, handle, length);
    if (ret) {
        return -ret;
    }

    vp->v_size = length;

    return 0;
}

static int nfs_op_readlink(struct vnode *vp, struct uio *uio)
{
    int err_no;
    auto nfs = get_nfs_context(vp, err_no);
    char buf[PATH_MAX + 1];

    if (err_no) {
        return err_no;
    }

    auto path = get_node_name(vp);

    memset(buf, 0, sizeof(buf));
    int ret = nfs_readlink(nfs, path, buf, sizeof(buf));
    if (ret) {
        return -ret;
    }

    size_t sz = MIN(uio->uio_iov->iov_len, strlen(buf) + 1);

    return uiomove(buf, sz, uio);
}

static int nfs_op_symlink(struct vnode *dvp, char *l, char *t)
{
    int err_no;
    auto nfs = get_nfs_context(dvp, err_no);
    auto target = mkpath(dvp, t);
    auto link = mkpath(dvp, l);

    if (err_no) {
        return err_no;
    }

    return -nfs_symlink(nfs, target.c_str(), link.c_str());
}

static  int nfs_op_inactive(struct vnode *)
{
    return 0;
}

#define nfs_op_ioctl       ((vnop_ioctl_t)vop_einval)      // not a device
#define nfs_op_seek        ((vnop_seek_t)vop_nullop)       // no special limits
#define nfs_op_link        ((vnop_link_t)vop_eperm)        // not in NFS
#define nfs_op_fallocate   ((vnop_fallocate_t)vop_nullop)  // not in NFS

/*
 * vnode operations
 */
struct vnops nfs_vnops = {
    nfs_op_open,             /* open */
    nfs_op_close,            /* close */
    nfs_op_read,             /* read */
    nfs_op_write,            /* write */
    nfs_op_seek,             /* seek */
    nfs_op_ioctl,            /* ioctl */
    nfs_op_fsync,            /* fsync */
    nfs_op_readdir,          /* readdir */
    nfs_op_lookup,           /* lookup */
    nfs_op_create,           /* create */
    nfs_op_remove,           /* remove */
    nfs_op_rename,           /* remame */
    nfs_op_mkdir,            /* mkdir */
    nfs_op_rmdir,            /* rmdir */
    nfs_op_getattr,          /* getattr */
    nfs_op_setattr,          /* setattr */
    nfs_op_inactive,         /* inactive */
    nfs_op_truncate,         /* truncate */
    nfs_op_link,             /* link */
    (vnop_cache_t) nullptr, /* arc */
    nfs_op_fallocate,        /* fallocate */
    nfs_op_readlink,         /* read link */
    nfs_op_symlink,          /* symbolic link */
};
