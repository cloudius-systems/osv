/*
 * Copyright (c) 2015 Carnegie Mellon University.
 * All Rights Reserved.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," WITH NO WARRANTIES WHATSOEVER. CARNEGIE
 * MELLON UNIVERSITY EXPRESSLY DISCLAIMS TO THE FULLEST EXTENT PERMITTEDBY LAW
 * ALL EXPRESS, IMPLIED, AND STATUTORY WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE, AND NON-INFRINGEMENT OF PROPRIETARY RIGHTS.
 *
 * Released under a modified BSD license. For full terms, please see mfs.txt in
 * the licenses folder or contact permi...@sei.cmu.edu.
 *
 * DM-0002621
 *
 * Based on https://github.com/jdroot/mfs
 *
 * Copyright (C) 2017 Waldemar Kozaczuk
 * Inspired by original MFS implementation by James Root from 2015
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

#include "rofs.hh"

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

int rofs_init(void) {
    return 0;
}

static int rofs_open(struct file *fp)
{
    if ((file_flags(fp) & FWRITE)) {
        // Do no allow opening files to write
        return (EROFS);
    }
    print("[rofs] rofs_open called for inode [%d] \n",
          ((struct rofs_inode *) fp->f_dentry.get()->d_vnode->v_data)->inode_no);
    return 0;
}

static int rofs_close(struct vnode *vp, struct file *fp) {
    print("[rofs] rofs_close called\n");
    // Nothing to do really...
    return 0;
}

//
// This function reads symbolic link information from directory structure in memory
// under rofs->symlinks table
static int rofs_readlink(struct vnode *vnode, struct uio *uio)
{
    struct rofs_info *rofs = (struct rofs_info *) vnode->v_mount->m_data;
    struct rofs_inode *inode = (struct rofs_inode *) vnode->v_data;

    if (!S_ISLNK(inode->mode)) {
        return EINVAL; //This node is not a symbolic link
    }

    assert(inode->data_offset >= 0 && inode->data_offset < rofs->sb->symlinks_count);

    char *link_path = rofs->symlinks[inode->data_offset];

    print("[rofs] rofs_readlink returned link [%s]\n", link_path);
    return uiomove(link_path, strlen(link_path), uio);
}

//
// This function reads as much data as requested per uio in single read from the disk but
// the data does not get retained for subsequent reads
static int rofs_read_without_cache(struct vnode *vnode, struct file *fp, struct uio *uio, int ioflag)
{
    struct rofs_info *rofs = (struct rofs_info *) vnode->v_mount->m_data;
    struct rofs_super_block *sb = rofs->sb;
    struct rofs_inode *inode = (struct rofs_inode *) vnode->v_data;
    struct device *device = vnode->v_mount->m_dev;

    VERIFY_READ_INPUT_ARGUMENTS()

    int rv = 0;
    int error = -1;
    uint64_t block = inode->data_offset;
    uint64_t offset = 0;

    // Total read amount is what they requested, or what is left
    uint64_t read_amt = std::min<uint64_t>(inode->file_size - uio->uio_offset, uio->uio_resid);

    // Calculate which block we need actually need to read
    block += uio->uio_offset / sb->block_size;
    offset = uio->uio_offset % sb->block_size;

    uint64_t block_count = (offset + read_amt) / sb->block_size;
    if ((offset + read_amt) % sb->block_size > 0)
        block_count++;

    void *buf = malloc(BSIZE * block_count);

    print("[rofs] rofs_read [%d], inode: %d, [%d -> %d] at %d of %d bytes\n",
          sched::thread::current()->id(), inode->inode_no, block, block_count, uio->uio_offset, read_amt);

    error = rofs_read_blocks(device, block, block_count, buf);

    if (error) {
        kprintf("[rofs_read] Error reading data\n");
        free(buf);
        return error;
    }

    rv = uiomove(buf + offset, read_amt, uio);

    free(buf);
    return rv;
}
//
// This version of read function reads more data than needed per uio following simple
// "read-around" logic. The data gets retained in cache and retrieved from memory
// by subsequent or contiguous reads. For details look at rofs_cache.cc.
static int rofs_read_with_cache(struct vnode *vnode, struct file* fp, struct uio *uio, int ioflag) {
    struct rofs_info *rofs = (struct rofs_info *) vnode->v_mount->m_data;
    struct rofs_super_block *sb = rofs->sb;
    struct rofs_inode *inode = (struct rofs_inode *) vnode->v_data;
    struct device *device = vnode->v_mount->m_dev;

    VERIFY_READ_INPUT_ARGUMENTS()

    return rofs::cache_read(inode,device,sb,uio);
}
//
// This functions reads directory information (dentries) based on information in memory
// under rofs->dir_entries table
static int rofs_readdir(struct vnode *vnode, struct file *fp, struct dirent *dir)
{
    struct rofs_info *rofs = (struct rofs_info *) vnode->v_mount->m_data;
    struct rofs_inode *inode = (struct rofs_inode *) vnode->v_data;

    uint64_t index = 0;

    if (!S_ISDIR(inode->mode)) {
        return ENOTDIR;
    }

    if (fp->f_offset == 0) {
        dir->d_type = DT_DIR;
        strlcpy((char *) &dir->d_name, ".", sizeof(dir->d_name));
    } else if (fp->f_offset == 1) {
        dir->d_type = DT_DIR;
        strlcpy((char *) &dir->d_name, "..", sizeof(dir->d_name));
    } else {
        index = fp->f_offset - 2;
        if (index >= inode->dir_children_count) {
            return ENOENT;
        }

        dir->d_fileno = fp->f_offset;

        // Set the name
        struct rofs_dir_entry *directory_entry = rofs->dir_entries + (inode->data_offset + index);
        strlcpy((char *) &dir->d_name, directory_entry->filename, sizeof(dir->d_name));
        dir->d_ino = directory_entry->inode_no;

        struct rofs_inode *directory_entry_inode = rofs->inodes + (dir->d_ino - 1);
        if (S_ISDIR(directory_entry_inode->mode))
            dir->d_type = DT_DIR;
        else if (S_ISLNK(directory_entry_inode->mode))
            dir->d_type = DT_LNK;
        else
            dir->d_type = DT_REG;
    }

    fp->f_offset++;

    return 0;
}

//
// This functions looks up directory entry based on the directory information stored in memory
// under rofs->dir_entries table
static int rofs_lookup(struct vnode *vnode, char *name, struct vnode **vpp)
{
    struct rofs_info *rofs = (struct rofs_info *) vnode->v_mount->m_data;
    struct rofs_inode *inode = (struct rofs_inode *) vnode->v_data;
    struct vnode *vp = nullptr;

    if (*name == '\0') {
        return ENOENT;
    }

    if (!S_ISDIR(inode->mode)) {
        print("[rofs] ABORTED lookup up %s at inode %d because not a directory\n", name, inode->inode_no);
        return ENOTDIR;
    }

    for (unsigned int idx = 0; idx < inode->dir_children_count; idx++) {
        if (strcmp(name, rofs->dir_entries[inode->data_offset + idx].filename) == 0) {
            int inode_no = rofs->dir_entries[inode->data_offset + idx].inode_no;

            if (vget(vnode->v_mount, inode_no, &vp)) { //TODO: Will it ever work? Revisit
                print("[rofs] found vp in cache!\n");
                *vpp = vp;
                return 0;
            }

            struct rofs_inode *found_inode = rofs->inodes + (inode_no - 1); //Check if exists
            rofs_set_vnode(vp, found_inode);

            print("[rofs] found the directory entry [%s] at at inode %d -> %d!\n", name, inode->inode_no,
                  found_inode->inode_no);

            *vpp = vp;
            return 0;
        }
    }

    print("[rofs] FAILED to find up %s\n", name);

    return ENOENT;
}

static int rofs_getattr(struct vnode *vnode, struct vattr *attr)
{
    struct rofs_inode *inode = (struct rofs_inode *) vnode->v_data;

    attr->va_mode = 0555;

    if (S_ISDIR(inode->mode)) {
        attr->va_type = VDIR;
    } else if (S_ISREG(inode->mode)) {
        attr->va_type = VREG;
    } else if (S_ISLNK(inode->mode)) {
        attr->va_type = VLNK;
    }

    attr->va_nodeid = vnode->v_ino;
    attr->va_size = vnode->v_size;

    auto *fsid = &vnode->v_mount->m_fsid;
    attr->va_fsid = ((uint32_t)fsid->__val[0]) | ((dev_t) ((uint32_t)fsid->__val[1]) << 32);

    return 0;
}

#define rofs_write       ((vnop_write_t)vop_erofs)
#define rofs_seek        ((vnop_seek_t)vop_nullop)
#define rofs_ioctl       ((vnop_ioctl_t)vop_nullop)
#define rofs_create      ((vnop_create_t)vop_erofs)
#define rofs_remove      ((vnop_remove_t)vop_erofs)
#define rofs_rename      ((vnop_rename_t)vop_erofs)
#define rofs_mkdir       ((vnop_mkdir_t)vop_erofs)
#define rofs_rmdir       ((vnop_rmdir_t)vop_erofs)
#define rofs_setattr     ((vnop_setattr_t)vop_erofs)
#define rofs_inactive    ((vnop_inactive_t)vop_nullop)
#define rofs_truncate    ((vnop_truncate_t)vop_erofs)
#define rofs_link        ((vnop_link_t)vop_erofs)
#define rofs_arc         ((vnop_cache_t) nullptr)
#define rofs_fallocate   ((vnop_fallocate_t)vop_erofs)
#define rofs_fsync       ((vnop_fsync_t)vop_nullop)
#define rofs_symlink     ((vnop_symlink_t)vop_erofs)

struct vnops rofs_vnops = {
    rofs_open,               /* open */
    rofs_close,              /* close */
    rofs_read_with_cache,    /* read */
    rofs_write,              /* write - returns error when called */
    rofs_seek,               /* seek */
    rofs_ioctl,              /* ioctl */
    rofs_fsync,              /* fsync */
    rofs_readdir,            /* readdir */
    rofs_lookup,             /* lookup */
    rofs_create,             /* create - returns error when called */
    rofs_remove,             /* remove - returns error when called */
    rofs_rename,             /* rename - returns error when called */
    rofs_mkdir,              /* mkdir - returns error when called */
    rofs_rmdir,              /* rmdir - returns error when called */
    rofs_getattr,            /* getattr */
    rofs_setattr,            /* setattr - returns error when called */
    rofs_inactive,           /* inactive */
    rofs_truncate,           /* truncate - returns error when called*/
    rofs_link,               /* link - returns error when called*/
    rofs_arc,                /* arc */ //TODO: Implement to allow memory re-use when mapping files
    rofs_fallocate,          /* fallocate - returns error when called*/
    rofs_readlink,           /* read link */
    rofs_symlink             /* symbolic link - returns error when called*/
};

extern "C" void rofs_disable_cache() {
    rofs_vnops.vop_read = rofs_read_without_cache;
}
