/*
 * Copyright (c) 2005-2007, Kohsuke Ohtani
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
 * vfs_mount.c - mount operations
 */

#include <sys/stat.h>
#include <dirent.h>

#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/device.h>
#include <osv/debug.h>
#include "vfs.h"

using namespace std;

/*
 * List for VFS mount points.
 */
static LIST_HEAD(, mount) mount_list = LIST_HEAD_INITIALIZER(mount_list);

/*
 * Global lock to access mount point.
 */
static mutex_t mount_lock = MUTEX_INITIALIZER;
#define MOUNT_LOCK()    mutex_lock(&mount_lock)
#define MOUNT_UNLOCK()  mutex_unlock(&mount_lock)

/*
 * Lookup file system.
 */
static const struct vfssw *
fs_getfs(char *name)
{
    const struct vfssw *fs;

    for (fs = vfssw; fs->vs_name; fs++) {
        if (!strncmp(name, fs->vs_name, FSMAXNAMES))
            break;
    }
    if (!fs->vs_name)
        return NULL;
    return fs;
}

const char*
fs_getfsname(vfsops* ops)
{
    for (auto fs = vfssw; fs->vs_name; fs++) {
        if (fs->vs_op == ops) {
            return fs->vs_name;
        }
    }
    abort();
}

int
sys_mount(char *dev, char *dir, char *fsname, int flags, void *data)
{
    const struct vfssw *fs;
    struct mount *mp;
    struct device *device;
    struct dentry *dp_covered;
    struct vnode *vp;
    int error;

    kprintf("VFS: mounting %s at %s\n", fsname, dir);

    if (!dir || *dir == '\0')
        return ENOENT;

    /* Find a file system. */
    if (!(fs = fs_getfs(fsname)))
        return ENODEV;  /* No such file system */

    /* Open device. NULL can be specified as a device. */
    // Allow device_open() to fail, in which case dev is interpreted
    // by the file system mount routine (e.g zfs pools)
    device = 0;
    if (dev && strncmp(dev, "/dev/", 5) == 0)
        device_open(dev + 5, DO_RDWR, &device);

    MOUNT_LOCK();

    /* Check if device or directory has already been mounted. */
    LIST_FOREACH(mp, &mount_list, m_link) {
        if (!strcmp(mp->m_path, dir) ||
            (device && mp->m_dev == device)) {
            error = EBUSY;  /* Already mounted */
            goto err1;
        }
    }
    /*
     * Create VFS mount entry.
     */
    if (!(mp = new mount)) {
        error = ENOMEM;
        goto err1;
    }
    mp->m_count = 0;
    mp->m_op = fs->vs_op;
    mp->m_flags = flags;
    mp->m_dev = device;
    strlcpy(mp->m_path, dir, sizeof(mp->m_path));
    strlcpy(mp->m_special, dev, sizeof(mp->m_special));

    /*
     * Get vnode to be covered in the upper file system.
     */
    if (*dir == '/' && *(dir + 1) == '\0') {
        /* Ignore if it mounts to global root directory. */
        dp_covered = NULL;
    } else {
        if ((error = namei(dir, &dp_covered)) != 0) {

            error = ENOENT;
            goto err2;
        }
        if (dp_covered->d_vnode->v_type != VDIR) {
            error = ENOTDIR;
            goto err3;
        }
    }
    mp->m_covered = dp_covered;

    /*
     * Create a root vnode for this file system.
     */
    if ((vp = vget(mp, "/")) == NULL) {
        error = ENOMEM;
        goto err3;
    }
    vp->v_type = VDIR;
    vp->v_flags = VROOT;
    vp->v_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;

    mp->m_root = dentry_alloc(vp, "/");
    if (!mp->m_root) {
        vput(vp);
        goto err3;
    }
    vput(vp);

    /*
     * Call a file system specific routine.
     */
    if ((error = VFS_MOUNT(mp, dev, flags, data)) != 0)
        goto err4;

    if (mp->m_flags & MNT_RDONLY)
        vp->v_mode &=~S_IWUSR;

    /*
     * Insert to mount list
     */
    LIST_INSERT_HEAD(&mount_list, mp, m_link);
    MOUNT_UNLOCK();

    return 0;   /* success */
 err4:
    drele(mp->m_root);
 err3:
    if (dp_covered)
        drele(dp_covered);
 err2:
    free(mp);
 err1:
    if (device)
        device_close(device);

    MOUNT_UNLOCK();
    return error;
}

int
sys_umount(const char *path)
{
    struct mount *mp;
    int error;

    DPRINTF(VFSDB_SYSCALL, ("sys_umount: path=%s\n", path));

    MOUNT_LOCK();

    /* Get mount entry */
    LIST_FOREACH(mp, &mount_list, m_link) {
        if (!strcmp(path, mp->m_path))
            goto found;
    }

    error = EINVAL;
    goto out;

found:
    /*
     * Root fs can not be unmounted.
     */
    if (mp->m_covered == NULL) {
        error = EINVAL;
        goto out;
    }
    if ((error = VFS_UNMOUNT(mp)) != 0)
        goto out;
    LIST_REMOVE(mp, m_link);

    /* Decrement referece count of root vnode */
    drele(mp->m_covered);

    /* Release all vnodes */
    vflush(mp);

#ifdef HAVE_BUFFERS
    /* Flush all buffers */
    binval(mp->m_dev);
#endif

    if (mp->m_dev)
        device_close(mp->m_dev);
    free(mp);
 out:
    MOUNT_UNLOCK();
    return error;
}

int
sys_sync(void)
{
    struct mount *mp;

    /* Call each mounted file system. */
    MOUNT_LOCK();
    LIST_FOREACH(mp, &mount_list, m_link)
        VFS_SYNC(mp);
    MOUNT_UNLOCK();
#ifdef HAVE_BUFFERS
    bio_sync();
#endif
    return 0;
}

/*
 * Compare two path strings. Return matched length.
 * @path: target path.
 * @root: vfs root path as mount point.
 */
static size_t
count_match(char *path, char *mount_root)
{
    size_t len = 0;

    while (*path && *mount_root) {
        if (*path++ != *mount_root++)
            break;
        len++;
    }
    if (*mount_root != '\0')
        return 0;

    if (len == 1 && *(path - 1) == '/')
        return 1;

    if (*path == '\0' || *path == '/')
        return len;
    return 0;
}

/*
 * Get the root directory and mount point for specified path.
 * @path: full path.
 * @mp: mount point to return.
 * @root: pointer to root directory in path.
 */
int
vfs_findroot(char *path, struct mount **mp, char **root)
{
    struct mount *m = NULL, *tmp;
    size_t len, max_len = 0;

    if (!path)
        return -1;

    /* Find mount point from nearest path */
    MOUNT_LOCK();
    LIST_FOREACH(tmp, &mount_list, m_link) {
        len = count_match(path, tmp->m_path);
        if (len > max_len) {
            max_len = len;
            m = tmp;
        }
    }
    MOUNT_UNLOCK();
    if (m == NULL)
        return -1;
    *root = (char *)(path + max_len);
    if (**root == '/')
        (*root)++;
    *mp = m;
    return 0;
}

/*
 * Mark a mount point as busy.
 */
void
vfs_busy(struct mount *mp)
{

    MOUNT_LOCK();
    mp->m_count++;
    MOUNT_UNLOCK();
}


/*
 * Mark a mount point as busy.
 */
void
vfs_unbusy(struct mount *mp)
{

    MOUNT_LOCK();
    mp->m_count--;
    MOUNT_UNLOCK();
}

int
vfs_nullop(void)
{
    return 0;
}

int
vfs_einval(void)
{
    return EINVAL;
}

namespace osv {

mount_desc to_mount_desc(mount* m)
{
    mount_desc ret;
    ret.special = m->m_special;
    ret.path = m->m_path;
    ret.type = fs_getfsname(m->m_op);
    // FIXME: record options
    ret.options = "";
    return ret;
}

vector<mount_desc>
current_mounts()
{
    WITH_LOCK(mount_lock) {
        vector<mount_desc> ret;
        struct mount *mp;
        LIST_FOREACH(mp, &mount_list, m_link) {
            ret.push_back(to_mount_desc(mp));
        }
        return ret;
    }
}

}

#ifdef DEBUG_VFS
void
mount_dump(void)
{
    struct mount *mp;

    MOUNT_LOCK();

    kprintf("mount_dump\n");
    kprintf("dev      count root\n");
    kprintf("-------- ----- --------\n");

    LIST_FOREACH(mp, &mount_list, m_link)
        kprintf("%8x %5d %s\n", mp->m_dev, mp->m_count, mp->m_path);
    MOUNT_UNLOCK();
}
#endif
