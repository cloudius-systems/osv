/*
 * Copyright (c) 2006-2007, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * rmafs_vnops.c - vnode operations for RAM file system.
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
#include <osv/vnode_attr.h>

#include "ramfs.h"

static mutex_t ramfs_lock = MUTEX_INITIALIZER;
static uint64_t inode_count = 1; /* inode 0 is reserved to root */

static void
set_times_to_now(struct timespec *time1, struct timespec *time2 = nullptr, struct timespec *time3 = nullptr)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    if (time1) {
        memcpy(time1, &now, sizeof(struct timespec));
    }
    if (time2) {
        memcpy(time2, &now, sizeof(struct timespec));
    }
    if (time3) {
        memcpy(time3, &now, sizeof(struct timespec));
    }
}

struct ramfs_node *
ramfs_allocate_node(const char *name, int type)
{
    struct ramfs_node *np;

    np = (ramfs_node *) malloc(sizeof(struct ramfs_node));
    if (np == NULL)
        return NULL;
    memset(np, 0, sizeof(struct ramfs_node));

    np->rn_namelen = strlen(name);
    np->rn_name = (char *) malloc(np->rn_namelen + 1);
    if (np->rn_name == NULL) {
        free(np);
        return NULL;
    }
    strlcpy(np->rn_name, name, np->rn_namelen + 1);
    np->rn_type = type;

    if (type == VDIR)
        np->rn_mode = S_IFDIR|0777;
    else if (type == VLNK)
        np->rn_mode = S_IFLNK|0777;
    else
        np->rn_mode = S_IFREG|0777;

    set_times_to_now(&(np->rn_ctime), &(np->rn_atime), &(np->rn_mtime));
    np->rn_owns_buf = true;
    np->rn_ref_count = 0;
    np->rn_removed = false;

    np->rn_file_segments_by_offset = new std::map<off_t,struct ramfs_file_segment>();
    np->rn_total_segments_size = 0;

    return np;
}

void
ramfs_free_node(struct ramfs_node *np)
{
    if (!np->rn_removed || np->rn_ref_count > 0) {
	    return;
    }

    if (np->rn_file_segments_by_offset != NULL) {
        if (np->rn_owns_buf) {
            for (auto it = np->rn_file_segments_by_offset->begin(); it != np->rn_file_segments_by_offset->end(); ++it) {
                free(it->second.data);
                it->second.data = NULL;
            }
        }

        delete np->rn_file_segments_by_offset;
    }


    free(np->rn_name);
    free(np);
}

static struct ramfs_node *
ramfs_add_node(struct ramfs_node *dnp, char *name, int type)
{
    struct ramfs_node *np, *prev;

    np = ramfs_allocate_node(name, type);
    if (np == NULL)
        return NULL;

    mutex_lock(&ramfs_lock);

    /* Link to the directory list */
    if (dnp->rn_child == NULL) {
        dnp->rn_child = np;
    } else {
        prev = dnp->rn_child;
        while (prev->rn_next != NULL)
            prev = prev->rn_next;
        prev->rn_next = np;
    }

    set_times_to_now(&(dnp->rn_mtime), &(dnp->rn_ctime));

    mutex_unlock(&ramfs_lock);
    return np;
}

static int
ramfs_remove_node(struct ramfs_node *dnp, struct ramfs_node *np)
{
    struct ramfs_node *prev;

    if (dnp->rn_child == NULL)
        return EBUSY;

    mutex_lock(&ramfs_lock);

    /* Unlink from the directory list */
    if (dnp->rn_child == np) {
        dnp->rn_child = np->rn_next;
    } else {
        for (prev = dnp->rn_child; prev->rn_next != np;
             prev = prev->rn_next) {
            if (prev->rn_next == NULL) {
                mutex_unlock(&ramfs_lock);
                return ENOENT;
            }
        }
        prev->rn_next = np->rn_next;
    }

    np->rn_removed = true;
    if (np->rn_ref_count <= 0) {
        ramfs_free_node(np);
    }

    set_times_to_now(&(dnp->rn_mtime), &(dnp->rn_ctime));

    mutex_unlock(&ramfs_lock);
    return 0;
}

static int
ramfs_rename_node(struct ramfs_node *np, char *name)
{
    size_t len;
    char *tmp;

    len = strlen(name);
    if (len > NAME_MAX) {
        return ENAMETOOLONG;
    }
    if (len <= np->rn_namelen) {
        /* Reuse current name buffer */
        strlcpy(np->rn_name, name, np->rn_namelen + 1);
    } else {
        /* Expand name buffer */
        tmp = (char *) malloc(len + 1);
        if (tmp == NULL)
            return ENOMEM;
        strlcpy(tmp, name, len + 1);
        free(np->rn_name);
        np->rn_name = tmp;
    }
    np->rn_namelen = len;
    set_times_to_now(&(np->rn_ctime));
    return 0;
}

static int
ramfs_lookup(struct vnode *dvp, char *name, struct vnode **vpp)
{
    struct ramfs_node *np, *dnp;
    struct vnode *vp;
    size_t len;
    int found;

    *vpp = NULL;

    if (*name == '\0')
        return ENOENT;

    mutex_lock(&ramfs_lock);

    len = strlen(name);
    dnp = (ramfs_node *) dvp->v_data;
    found = 0;
    for (np = dnp->rn_child; np != NULL; np = np->rn_next) {
        if (np->rn_namelen == len &&
            memcmp(name, np->rn_name, len) == 0) {
            found = 1;
            break;
        }
    }
    if (found == 0) {
        mutex_unlock(&ramfs_lock);
        return ENOENT;
    }
    if (vget(dvp->v_mount, inode_count++, &vp)) {
        /* found in cache */
        *vpp = vp;
        mutex_unlock(&ramfs_lock);
        return 0;
    }
    if (!vp) {
        mutex_unlock(&ramfs_lock);
        return ENOMEM;
    }
    vp->v_data = np;
    vp->v_mode = ALLPERMS;
    vp->v_type = np->rn_type;
    vp->v_size = np->rn_size;

    mutex_unlock(&ramfs_lock);

    *vpp = vp;

    return 0;
}

static int
ramfs_mkdir(struct vnode *dvp, char *name, mode_t mode)
{
    struct ramfs_node *np;

    DPRINTF(("mkdir %s\n", name));
    if (strlen(name) > NAME_MAX) {
        return ENAMETOOLONG;
    }

    if (!S_ISDIR(mode))
        return EINVAL;

    np = (ramfs_node *) ramfs_add_node((ramfs_node *) dvp->v_data, name, VDIR);
    if (np == NULL)
        return ENOMEM;
    np->rn_size = 0;

    return 0;
}

static int
ramfs_symlink(struct vnode *dvp, char *name, char *link)
{
    if (strlen(name) > NAME_MAX) {
        return ENAMETOOLONG;
    }
    auto np = ramfs_add_node((ramfs_node *) dvp->v_data, name, VLNK);
    if (np == NULL)
        return ENOMEM;
    // Save the link target without the final null, as readlink() wants it.
    size_t len = strlen(link);
    np->rn_size = len;

    ramfs_file_segment new_segment = {
        .size = len,
        .data = strndup(link, len)
    };
    (*np->rn_file_segments_by_offset)[0] = new_segment;
    np->rn_total_segments_size = len;

    return 0;
}

static int
ramfs_readlink(struct vnode *vp, struct uio *uio)
{
    struct ramfs_node *np = (ramfs_node *) vp->v_data;
    size_t len;

    if (vp->v_type != VLNK) {
        return EINVAL;
    }
    if (uio->uio_offset < 0) {
        return EINVAL;
    }
    if (uio->uio_resid == 0) {
        return 0;
    }
    if (uio->uio_offset >= (off_t) vp->v_size)
        return 0;
    if (vp->v_size - uio->uio_offset < uio->uio_resid)
        len = vp->v_size - uio->uio_offset;
    else
        len = uio->uio_resid;

    set_times_to_now( &(np->rn_atime));
    return uiomove((*np->rn_file_segments_by_offset)[0].data + uio->uio_offset, len, uio);
}

/* Remove a directory */
static int
ramfs_rmdir(struct vnode *dvp, struct vnode *vp, char *name)
{
    return ramfs_remove_node((ramfs_node *) dvp->v_data, (ramfs_node *) vp->v_data);
}

/* Remove a file */
static int
ramfs_remove(struct vnode *dvp, struct vnode *vp, char *name)
{
    DPRINTF(("remove %s in %s\n", name, dvp->v_path));
    return ramfs_remove_node((ramfs_node *) dvp->v_data, (ramfs_node *) vp->v_data);
}

#define ENLARGE_FACTOR 1.1f
static int
ramfs_enlarge_data_buffer(struct ramfs_node *np, size_t desired_length)
{
    // New total size has to be at least greater by the ENLARGE_FACTOR
    auto new_total_segment_size = round_page(std::max<size_t>(np->rn_total_segments_size * ENLARGE_FACTOR, desired_length));
    assert(new_total_segment_size >= desired_length);

    auto new_segment_size = np->rn_owns_buf ? new_total_segment_size - np->rn_total_segments_size : new_total_segment_size;
    ramfs_file_segment new_segment = {
        .size = new_segment_size,
        .data = (char*) malloc(new_segment_size)
    };
    if (!new_segment.data)
        return EIO;

    auto new_segment_file_offset = np->rn_total_segments_size;
    if (!np->rn_owns_buf) {
        // This is a file from bootfs and we cannot simply enlarge same way
	// as regular one because because original segment is not be page-aligned.
	// Therefore we need to created new segment and copy content of the old one
        memcpy(new_segment.data, (*np->rn_file_segments_by_offset)[0].data, np->rn_size);
        new_segment_file_offset = 0;
    }

    // Insert new segment with the file offset it starts at which happends
    // to be old total segments size
    (*np->rn_file_segments_by_offset)[new_segment_file_offset] = new_segment;
    np->rn_total_segments_size = new_total_segment_size;
    np->rn_owns_buf = true;

    return 0;
}

/* Truncate file */
static int
ramfs_truncate(struct vnode *vp, off_t length)
{
    struct ramfs_node *np;

    DPRINTF(("truncate %s length=%d\n", vp->v_path, length));
    np = (ramfs_node *) vp->v_data;

    //TODO: Shall we free segments id new length < np->total_segments_size
    if (length == 0) {
        if (np->rn_owns_buf) {
            for (auto it = (*np->rn_file_segments_by_offset).begin(); it != (*np->rn_file_segments_by_offset).end(); ++it) {
                free(it->second.data);
            }
        }

        (*np->rn_file_segments_by_offset).clear();
        np->rn_total_segments_size = 0;
    } else if (size_t(length) > np->rn_total_segments_size) {
        auto ret = ramfs_enlarge_data_buffer(np, length);
        if (ret) {
            return ret;
        }
    }

    np->rn_size = length;
    vp->v_size = length;

    set_times_to_now(&(np->rn_mtime), &(np->rn_ctime));
    return 0;
}

/*
 * Create empty file.
 */
static int
ramfs_create(struct vnode *dvp, char *name, mode_t mode)
{
    struct ramfs_node *np;

    if (strlen(name) > NAME_MAX) {
        return ENAMETOOLONG;
    }

    DPRINTF(("create %s in %s\n", name, dvp->v_path));
    if (!S_ISREG(mode))
        return EINVAL;

    np = ramfs_add_node((ramfs_node *) dvp->v_data, name, VREG);
    if (np == NULL)
        return ENOMEM;
    return 0;
}

static int
ramfs_read_or_write_file_data(std::map<off_t,struct ramfs_file_segment> &file_segments_by_offset, struct uio *uio, size_t bytes_to_read_or_write)
{
    // Find the segment where we need to start reading from or writing to based on uio_offset
    auto segment_to_read_or_write = file_segments_by_offset.lower_bound(uio->uio_offset);
    if( segment_to_read_or_write == file_segments_by_offset.end()) {
        segment_to_read_or_write = --file_segments_by_offset.end();
    }
    else if (segment_to_read_or_write->first > uio->uio_offset) {
        segment_to_read_or_write--;
    }
    assert(uio->uio_offset >= segment_to_read_or_write->first &&
           uio->uio_offset < (off_t)(segment_to_read_or_write->first + segment_to_read_or_write->second.size));

    // Simply iterate starting with initial identified segment above to read or write
    // until we have read all bytes or have iterated all segments
    uint64_t file_offset = uio->uio_offset;
    for (; segment_to_read_or_write != file_segments_by_offset.end() && bytes_to_read_or_write > 0; segment_to_read_or_write++) {
        // First calculate where we need to start in this segment ...
        auto offset_in_segment = file_offset - segment_to_read_or_write->first;
        // .. then calculate how many bytes to read or write in this segment
        auto maximum_bytes_to_read_or_write_in_segment = segment_to_read_or_write->second.size - offset_in_segment;
        auto bytes_to_read_or_write_in_segment = std::min<uint64_t>(bytes_to_read_or_write,maximum_bytes_to_read_or_write_in_segment);
        assert(offset_in_segment >= 0 && offset_in_segment < segment_to_read_or_write->second.size);
        auto ret = uiomove(segment_to_read_or_write->second.data + offset_in_segment, bytes_to_read_or_write_in_segment, uio);
        if (ret) {
            return ret;
        }
        bytes_to_read_or_write -= bytes_to_read_or_write_in_segment;
        file_offset += bytes_to_read_or_write_in_segment;
    }

    return 0;
}

static int
ramfs_read(struct vnode *vp, struct file *fp, struct uio *uio, int ioflag)
{
    struct ramfs_node *np = (ramfs_node *) vp->v_data;
    size_t len;

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

    if (uio->uio_offset >= (off_t) vp->v_size)
        return 0;

    if (vp->v_size - uio->uio_offset < uio->uio_resid)
        len = vp->v_size - uio->uio_offset;
    else
        len = uio->uio_resid;

    set_times_to_now(&(np->rn_atime));

    return ramfs_read_or_write_file_data(*(np->rn_file_segments_by_offset),uio,len);
}

int
ramfs_set_file_data(struct vnode *vp, const void *data, size_t size)
{
    struct ramfs_node *np = (ramfs_node *) vp->v_data;

    if (vp->v_type == VDIR) {
        return EISDIR;
    }
    if (vp->v_type != VREG) {
        return EINVAL;
    }
    if (!(*np->rn_file_segments_by_offset).empty()) {
        return EINVAL;
    }

    ramfs_file_segment new_segment = {
        .size = size,
        .data = (char *) data
    };
    (*np->rn_file_segments_by_offset)[0] = new_segment;
    np->rn_total_segments_size = size;

    np->rn_size = size;
    np->rn_owns_buf = false;

    vp->v_size = size;

    return 0;
}

static int
ramfs_write(struct vnode *vp, struct uio *uio, int ioflag)
{
    struct ramfs_node *np = (ramfs_node *) vp->v_data;

    if (vp->v_type == VDIR) {
        return EISDIR;
    }
    if (vp->v_type != VREG) {
        return EINVAL;
    }
    if (uio->uio_offset < 0) {
        return EINVAL;
    }
    if (uio->uio_offset >= LONG_MAX) {
        return EFBIG;
    }
    if (uio->uio_resid == 0) {
        return 0;
    }

    if (ioflag & IO_APPEND)
        uio->uio_offset = np->rn_size;

    if (size_t(uio->uio_offset + uio->uio_resid) > (size_t) vp->v_size) {
        /* Expand the file size before writing to it */
        off_t end_pos = uio->uio_offset + uio->uio_resid;
        if (end_pos > (off_t) np->rn_total_segments_size) {
            auto ret = ramfs_enlarge_data_buffer(np, end_pos);
            if (ret) {
                return ret;
            }
        }
        np->rn_size = end_pos;
        vp->v_size = end_pos;
    }

    set_times_to_now(&(np->rn_mtime), &(np->rn_ctime));

    assert(uio->uio_offset + uio->uio_resid <= (off_t)(np->rn_total_segments_size));
    return ramfs_read_or_write_file_data(*(np->rn_file_segments_by_offset),uio,uio->uio_resid);
}

static int
ramfs_rename(struct vnode *dvp1, struct vnode *vp1, char *name1,
             struct vnode *dvp2, struct vnode *vp2, char *name2)
{
    struct ramfs_node *np, *old_np;
    int error;

    if (vp2) {
        /* Remove destination file, first */
        error = ramfs_remove_node((ramfs_node *) dvp2->v_data, (ramfs_node *) vp2->v_data);
        if (error)
            return error;
    }
    /* Same directory ? */
    if (dvp1 == dvp2) {
        /* Change the name of existing file */
        error = ramfs_rename_node((ramfs_node *) vp1->v_data, name2);
        if (error)
            return error;
    } else {
        /* Create new file or directory */
        old_np = (ramfs_node *) vp1->v_data;
        np = ramfs_add_node((ramfs_node *) dvp2->v_data, name2, old_np->rn_type);
        if (np == NULL)
            return ENOMEM;

        np->rn_size = old_np->rn_size;
        np->rn_file_segments_by_offset = old_np->rn_file_segments_by_offset;
        np->rn_total_segments_size = old_np->rn_total_segments_size;
        np->rn_owns_buf = old_np->rn_owns_buf;

        old_np->rn_file_segments_by_offset = NULL;
        old_np->rn_owns_buf = false;

        /* Remove source file */
        ramfs_remove_node((ramfs_node *) dvp1->v_data, (ramfs_node *) vp1->v_data);
    }
    return 0;
}

/*
 * @vp: vnode of the directory.
 */
static int
ramfs_readdir(struct vnode *vp, struct file *fp, struct dirent *dir)
{
    struct ramfs_node *np, *dnp;
    int i;

    mutex_lock(&ramfs_lock);

    set_times_to_now(&(((ramfs_node *) vp->v_data)->rn_atime));

    if (fp->f_offset == 0) {
        dir->d_type = DT_DIR;
        strlcpy((char *) &dir->d_name, ".", sizeof(dir->d_name));
    } else if (fp->f_offset == 1) {
        dir->d_type = DT_DIR;
        strlcpy((char *) &dir->d_name, "..", sizeof(dir->d_name));
    } else {
        dnp = (ramfs_node *) vp->v_data;
        np = dnp->rn_child;
        if (np == NULL) {
            mutex_unlock(&ramfs_lock);
            return ENOENT;
        }

        for (i = 0; i != (fp->f_offset - 2); i++) {
            np = np->rn_next;
            if (np == NULL) {
                mutex_unlock(&ramfs_lock);
                return ENOENT;
            }
        }
        if (np->rn_type == VDIR)
            dir->d_type = DT_DIR;
        else if (np->rn_type == VLNK)
            dir->d_type = DT_LNK;
        else
            dir->d_type = DT_REG;
        strlcpy((char *) &dir->d_name, np->rn_name,
                sizeof(dir->d_name));
    }
    dir->d_fileno = fp->f_offset;
//	dir->d_namelen = strlen(dir->d_name);

    fp->f_offset++;

    mutex_unlock(&ramfs_lock);
    return 0;
}

int
ramfs_init(void)
{
    return 0;
}

static int
ramfs_getattr(struct vnode *vnode, struct vattr *attr)
{
    attr->va_nodeid = vnode->v_ino;
    attr->va_size = vnode->v_size;

    struct ramfs_node *np = (ramfs_node *) vnode->v_data;
    attr->va_type = (vtype) np->rn_type;

    memcpy(&(attr->va_atime), &(np->rn_atime), sizeof(struct timespec));
    memcpy(&(attr->va_ctime), &(np->rn_ctime), sizeof(struct timespec));
    memcpy(&(attr->va_mtime), &(np->rn_mtime), sizeof(struct timespec));

    attr->va_mode = np->rn_mode;

    return 0;
}

static int
ramfs_setattr(struct vnode *vnode, struct vattr *attr) {
    struct ramfs_node *np = (ramfs_node *) vnode->v_data;

    if (attr->va_mask & AT_ATIME) {
        memcpy(&(np->rn_atime), &(attr->va_atime), sizeof(struct timespec));
    }

    if (attr->va_mask & AT_CTIME) {
        memcpy(&(np->rn_ctime), &(attr->va_ctime), sizeof(struct timespec));
    }

    if (attr->va_mask & AT_MTIME) {
        memcpy(&(np->rn_mtime), &(attr->va_mtime), sizeof(struct timespec));
    }

    if (attr->va_mask & AT_MODE) {
        np->rn_mode = attr->va_mode;
    }

    return 0;
}

int ramfs_open(struct file *fp)
{
    struct vnode *vp = file_dentry(fp)->d_vnode;
    struct ramfs_node *np = (ramfs_node *) vp->v_data;
    np->rn_ref_count++;
    return 0;
}

int ramfs_close(struct vnode *dvp, struct file *file)
{
    struct vnode *vp = file_dentry(file)->d_vnode;
    struct ramfs_node *np = (ramfs_node *) vp->v_data;
    np->rn_ref_count--;
    ramfs_free_node(np);
    return 0;
}

#define ramfs_seek      ((vnop_seek_t)vop_nullop)
#define ramfs_ioctl     ((vnop_ioctl_t)vop_einval)
#define ramfs_fsync     ((vnop_fsync_t)vop_nullop)
#define ramfs_inactive  ((vnop_inactive_t)vop_nullop)
#define ramfs_link      ((vnop_link_t)vop_eperm)
#define ramfs_fallocate ((vnop_fallocate_t)vop_nullop)

/*
 * vnode operations
 */
struct vnops ramfs_vnops = {
        ramfs_open,             /* open */
        ramfs_close,            /* close */
        ramfs_read,             /* read */
        ramfs_write,            /* write */
        ramfs_seek,             /* seek */
        ramfs_ioctl,            /* ioctl */
        ramfs_fsync,            /* fsync */
        ramfs_readdir,          /* readdir */
        ramfs_lookup,           /* lookup */
        ramfs_create,           /* create */
        ramfs_remove,           /* remove */
        ramfs_rename,           /* remame */
        ramfs_mkdir,            /* mkdir */
        ramfs_rmdir,            /* rmdir */
        ramfs_getattr,          /* getattr */
        ramfs_setattr,          /* setattr */
        ramfs_inactive,         /* inactive */
        ramfs_truncate,         /* truncate */
        ramfs_link,             /* link */
        (vnop_cache_t) nullptr, /* arc */
        ramfs_fallocate,        /* fallocate */
        ramfs_readlink,         /* read link */
        ramfs_symlink,          /* symbolic link */
};

