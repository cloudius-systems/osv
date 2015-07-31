/*
 * Copyright 2015 Carnegie Mellon University
 * This material is based upon work funded and supported by the Department of
 * Defense under Contract No. FA8721-05-C-0003 with Carnegie Mellon University
 * for the operation of the Software Engineering Institute, a federally funded
 * research and development center.
 * 
 * Any opinions, findings and conclusions or recommendations expressed in this
 * material are those of the author(s) and do not necessarily reflect the views
 * of the United States Department of Defense.
 * 
 * NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
 * INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
 * UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS
 * TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE
 * OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE
 * MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND
 * WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
 * 
 * This material has been approved for public release and unlimited
 * distribution.
 * 
 * DM-0002621
 *
 * Based on https://github.com/jdroot/mfs
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

#include "mfs.hh"

// Used by extern declaration in fs/vfs/vfs_conf.cc
int mfs_init(void) {
    return 0;
}

static int mfs_open(struct file *fp) {
    if ((file_flags(fp) & FWRITE)) {
        // We do not allow writing! jerks
        return (EPERM);
    }
    return 0;
}

static int mfs_close(struct vnode *vp, struct file *fp) {
    print("[mfs] mfs_close called\n");
    // Nothing to do really...
    return 0;
}

static size_t min(size_t a, size_t b) {
    if (a > b) return b;
    return a;
}

// FIXME: The current link implementation is very wasteful as far as disk
// space goes. It leaves 512 - path length bytes un-used. For a base OSv
// image this is not a big deal, because it has exactly 1 link, but if
// an image is created that uses many links, it could start to be a large,
// waste of space. I think the least intrusive way to add more is to write
// links sequentially (with \0 as the last character) and store their
// data_block_number and offset in the corresponding inode. This makes
// the generation script a little bit more complex.
static int mfs_readlink(struct vnode *vnode, struct uio *uio) {
    struct mfs             *mfs    = (struct mfs*) vnode->v_mount->m_data;
    struct mfs_inode       *inode  = (struct mfs_inode*)vnode->v_data;
    struct device          *device = vnode->v_mount->m_dev;
    struct buf             *bh     = nullptr;
    char                   *data   = nullptr;

    int error = -1;

    error = mfs_cache_read(mfs, device, inode->data_block_number, &bh);
    if (error) {
        kprintf("[mfs] Error reading link from inode->data_block_number\n");
        return error;
    }

    data = (char *)bh->b_data;
    error = uiomove(data, strlen(data) + 1, uio);
    mfs_cache_release(mfs, bh);
    return error;
}

static int mfs_read(struct vnode *vnode, struct file* fp, struct uio *uio, int ioflag) {
    struct mfs             *mfs    = (struct mfs*) vnode->v_mount->m_data;
    struct mfs_super_block *sb     = mfs->sb;
    struct mfs_inode       *inode  = (struct mfs_inode*)vnode->v_data;
    struct device          *device = vnode->v_mount->m_dev;
    struct buf             *bh     = nullptr;
    char                   *data   = nullptr;

    size_t   len    =  0;
    int      rv     =  0;
    int      error  = -1;
    uint64_t block  =  inode->data_block_number;
    uint64_t offset =  0;

    // Total read amount is what they requested, or what is left
    uint64_t read_amt = min(inode->file_size - uio->uio_offset, uio->uio_resid);
    uint64_t total  =  0;

    // Calculate which block we need actually need to read
    block += uio->uio_offset / sb->block_size;
    offset = uio->uio_offset % sb->block_size;

    // Cant read directories
    if (vnode->v_type == VDIR)
        return EISDIR;
    // Cant read anything but reg
    if (vnode->v_type != VREG)
        return EINVAL;
    // Cant start reading before the first byte
    if (uio->uio_offset < 0)
        return EINVAL;
    // Need to read more than 1 byte
    if (uio->uio_resid == 0)
        return 0;
    // Cant read after the end of the file
    if (uio->uio_offset >= (off_t)vnode->v_size)
        return 0;

    while (read_amt > 0) {
        // Force the read to fit inside a block
        len = min(sb->block_size - offset, read_amt);

        error = mfs_cache_read(mfs, device, block, &bh);
        if (error) {
            kprintf("[mfs] Error reading block [%llu]\n", block);
            return 0;
        }

        data = (char *)bh->b_data;
        rv = uiomove(data + offset, len, uio);
        mfs_cache_release(mfs, bh);

        // Move on to the next block
        // Set offset to 0 to make sure we start the start of the next block
        offset    = 0;
        read_amt -= len;
        total    += len;
        block++;
    }

    return rv;
}

static int mfs_readdir(struct vnode *vnode, struct file *fp, struct dirent *dir) {

    struct mfs             *mfs    = (struct mfs*)vnode->v_mount->m_data;
    struct mfs_inode       *inode  = (struct mfs_inode*)vnode->v_data;
    struct mfs_super_block *sb     = mfs->sb;
    struct device          *device = vnode->v_mount->m_dev;
    struct mfs_dir_record  *record = nullptr;
    struct buf             *bh     = nullptr;
    
    int      error  = -1;
    uint64_t index  =  0;
    uint64_t block  =  inode->data_block_number;
    uint64_t offset =  0;
    
    if (fp->f_offset == 0) {
        dir->d_type = DT_DIR;
        strlcpy((char *)&dir->d_name, ".", sizeof(dir->d_name));
    } else if (fp->f_offset == 1) {
        dir->d_type = DT_DIR;
        strlcpy((char *)&dir->d_name, "..", sizeof(dir->d_name));
    } else {
        
        index = fp->f_offset - 2;
        if (index >= inode->dir_children_count) {
            return ENOENT;
        }
    
        block  += MFS_RECORD_BLOCK(sb->block_size, index);
        offset  = MFS_RECORD_OFFSET(sb->block_size, index);

        print("[mfs] readdir block: %llu\n", block);
        print("[mfs] readdir offset: %llu\n", offset);

        // Do as much as possible before the read
        if (S_ISDIR(inode->mode))
            dir->d_type = DT_DIR;
        else
            dir->d_type = DT_REG;
    
        dir->d_fileno = fp->f_offset;


        error = mfs_cache_read(mfs, device, block, &bh);
        if (error) {
            kprintf("[mfs] Error reading block [%llu]\n", block);
            return ENOENT;
        }

        record = (struct mfs_dir_record*)bh->b_data;
        record += offset;

        // Set the name
        strlcpy((char *)&dir->d_name, record->filename, sizeof(dir->d_name));
        dir->d_ino = record->inode_no;

        mfs_cache_release(mfs, bh);
    }

    fp->f_offset++;

    return 0;
}

static int mfs_lookup(struct vnode *vnode, char *name, struct vnode **vpp) {
    struct mfs             *mfs     = (struct mfs*)vnode->v_mount->m_data;
    struct mfs_inode       *inode   = (struct mfs_inode*)vnode->v_data;
    struct mfs_super_block *sb      = mfs->sb;
    struct device          *device  = vnode->v_mount->m_dev;
    struct mfs_inode       *r_inode = nullptr;
    struct mfs_dir_record  *records = nullptr;
    struct buf             *bh      = nullptr;
    struct vnode           *vp      = nullptr;

    int      error  = -1;
    uint64_t i      =  0;
    uint64_t block  =  inode->data_block_number;
    uint64_t c      =  0;

    if (*name == '\0') {
        return ENOENT;
    }

    while (r_inode == nullptr) {
        error = mfs_cache_read(mfs, device, block, &bh);
        if (error) {
            kprintf("[mfs] Error reading block [%llu]\n", block);
            return ENOENT;
        }

        records = (struct mfs_dir_record *)bh->b_data;
        for (i = 0; i < MFS_RECORDS_PER_BLOCK(sb->block_size); i++) {
            if (strcmp(name, records[i].filename) == 0) {
                // Found!
                print("[mfs] found the directory entry!\n");
                r_inode = mfs_get_inode(mfs, device, records[i].inode_no);
                break;
            }
            c++;
            if (c >= inode->dir_children_count) {
                break;
            }
        }

        mfs_cache_release(mfs, bh);

        // If we looked at every entry and still havnt found it
        if (c >= inode->dir_children_count && r_inode == nullptr) {
            return ENOENT;
        } else {
            // Move on to the next block!
            block++;
        }
    }

    print("[mfs] mfs_lookup using inode: %llu\n", r_inode->inode_no);

    if (vget(vnode->v_mount, r_inode->inode_no, &vp)) {
        print("[mfs] found vp in cache!\n");
        // Found in cache?
        *vpp = vp;
        return 0;
    }

    print("[mfs] got vp: %p\n", vp);

    if (!vp) {
        delete r_inode;
        return ENOMEM;
    }

    mfs_set_vnode(vp, r_inode);

    *vpp = vp;

    return 0;
}

static int mfs_getattr(struct vnode *vnode, struct vattr *attr) {
    struct mfs_inode *inode = (struct mfs_inode*)vnode->v_data;

    // Doesn't seem to work, I think permissions are hard coded to 777
    attr->va_mode = 00555;
    
    if (S_ISDIR(inode->mode)) {
        attr->va_type = VDIR;
    } else {
        attr->va_type = VREG;
    }

    attr->va_nodeid = vnode->v_ino;
    attr->va_size = vnode->v_size;

    return 0;
}

#define mfs_seek        ((vnop_seek_t)vop_nullop)
#define mfs_ioctl        ((vnop_ioctl_t)vop_nullop)
#define mfs_inactive    ((vnop_inactive_t)vop_nullop)
#define mfs_truncate    ((vnop_truncate_t)vop_nullop)
#define mfs_link         ((vnop_link_t)vop_nullop)
#define mfs_arc            ((vnop_cache_t) nullptr)
#define mfs_fallocate    ((vnop_fallocate_t)vop_nullop)
#define mfs_fsync        ((vnop_fsync_t)vop_nullop)
#define mfs_symlink        ((vnop_symlink_t)vop_nullop)

struct vnops mfs_vnops = {
    mfs_open,       /* open */
    mfs_close,      /* close */
    mfs_read,       /* read */
    nullptr,           /* write - not impelemented */
    mfs_seek,       /* seek */
    mfs_ioctl,      /* ioctl */
    mfs_fsync,      /* fsync */
    mfs_readdir,    /* readdir */
    mfs_lookup,     /* lookup */
    nullptr,        /* create - not impelemented */
    nullptr,        /* remove - not impelemented */
    nullptr,        /* rename - not impelemented */
    nullptr,        /* mkdir - not impelemented */
    nullptr,        /* rmdir - not impelemented */
    mfs_getattr,    /* getattr */
    nullptr,        /* setattr - not impelemented */
    mfs_inactive,   /* inactive */
    mfs_truncate,   /* truncate */
    mfs_link,       /* link */
    mfs_arc,        /* arc */
    mfs_fallocate,  /* fallocate */
    mfs_readlink,   /* read link */
    mfs_symlink     /* symbolic link */
};
