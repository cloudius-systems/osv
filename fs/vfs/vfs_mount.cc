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
#include <sys/param.h>
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
#include <osv/mutex.h>
#include "vfs.h"

#include <memory>
#include <list>

/*
 * List for VFS mount points.
 */
static std::list<mount*> mount_list;

/*
 * Global lock to access mount point.
 */
static mutex mount_lock;

/*
 * Lookup file system.
 */
static const struct vfssw *
fs_getfs(const char *name)
{
    const struct vfssw *fs;

    for (fs = vfssw; fs->vs_name; fs++) {
        if (!strncmp(name, fs->vs_name, FSMAXNAMES))
            break;
    }
    if (!fs->vs_name)
        return nullptr;
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
sys_mount(const char *dev, const char *dir, const char *fsname, int flags, const void *data)
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

    /* Open device. nullptr can be specified as a device. */
    // Allow device_open() to fail, in which case dev is interpreted
    // by the file system mount routine (e.g zfs pools)
    device = 0;
    if (dev && strncmp(dev, "/dev/", 5) == 0)
        device_open(dev + 5, DO_RDWR, &device);

    SCOPE_LOCK(mount_lock);

    /* Check if device or directory has already been mounted. */
    for (auto&& mp : mount_list) {
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
    mp->m_data = nullptr;
    strlcpy(mp->m_path, dir, sizeof(mp->m_path));
    strlcpy(mp->m_special, dev, sizeof(mp->m_special));

    /*
     * Get vnode to be covered in the upper file system.
     */
    if (*dir == '/' && *(dir + 1) == '\0') {
        /* Ignore if it mounts to global root directory. */
        dp_covered = nullptr;
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
    vget(mp, 0, &vp);
    if (vp == nullptr) {
        error = ENOMEM;
        goto err3;
    }
    vp->v_type = VDIR;
    vp->v_flags = VROOT;
    vp->v_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;

    mp->m_root = dentry_alloc(nullptr, vp, "/");
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
    mount_list.push_back(mp);

    return 0;   /* success */
 err4:
    drele(mp->m_root);
 err3:
    if (dp_covered)
        drele(dp_covered);
 err2:
    delete mp;
 err1:
    if (device)
        device_close(device);

    return error;
}

void
release_mp_dentries(struct mount *mp)
{
    /* Decrement referece count of root vnode */
    if (mp->m_covered) {
        drele(mp->m_covered);
    }

    /* Release root dentry */
    drele(mp->m_root);
}

int
sys_umount2(const char *path, int flags)
{
    struct mount *mp;
    int error, pathlen;

    kprintf("VFS: unmounting %s\n", path);

    SCOPE_LOCK(mount_lock);

    pathlen = strlen(path);
    if (pathlen >= MAXPATHLEN) {
        error = ENAMETOOLONG;
        goto out;
    }

    /* Get mount entry */
    for (auto&& tmp : mount_list) {
        if (!strcmp(path, tmp->m_path)) {
            mp = tmp;
            goto found;
        }
    }

    error = EINVAL;
    goto out;

found:
    /*
     * Root fs can not be unmounted.
     */
    if (mp->m_covered == nullptr && !(flags & MNT_FORCE)) {
        error = EINVAL;
        goto out;
    }

    if ((error = VFS_UNMOUNT(mp, flags)) != 0)
        goto out;
    mount_list.remove(mp);

#ifdef HAVE_BUFFERS
    /* Flush all buffers */
    binval(mp->m_dev);
#endif

    if (mp->m_dev)
        device_close(mp->m_dev);
    delete mp;
 out:
    return error;
}

int
sys_umount(const char *path)
{
    return sys_umount2(path, 0);
}

int
sys_pivot_root(const char *new_root, const char *put_old)
{
    struct mount *newmp = nullptr, *oldmp = nullptr;
    int error;

    WITH_LOCK(mount_lock) {
        for (auto&& mp : mount_list) {
            if (!strcmp(mp->m_path, new_root)) {
                newmp = mp;
            }
            if (!strcmp(mp->m_path, put_old)) {
                oldmp = mp;
            }
        }
        if (!newmp || !oldmp || newmp == oldmp) {
            return EINVAL;
        }
        for (auto&& mp : mount_list) {
            if (mp == newmp || mp == oldmp) {
                continue;
            }
            if (!strncmp(mp->m_path, put_old, strlen(put_old))) {
                return EBUSY;
            }
        }
        if ((error = VFS_UNMOUNT(oldmp, 0)) != 0) {
            return error;
        }
        mount_list.remove(oldmp);

        newmp->m_root->d_vnode->v_mount = newmp;

        if (newmp->m_covered) {
            drele(newmp->m_covered);
        }
        newmp->m_covered = nullptr;

        if (newmp->m_root->d_parent) {
            drele(newmp->m_root->d_parent);
        }
        newmp->m_root->d_parent = nullptr;

        strlcpy(newmp->m_path, "/", sizeof(newmp->m_path));
    }
    return 0;
}

int
sys_sync(void)
{
    /* Call each mounted file system. */
    WITH_LOCK(mount_lock) {
        for (auto&& mp : mount_list) {
            VFS_SYNC(mp);
        }
    }
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
count_match(const char *path, char *mount_root)
{
    size_t len = 0;

    while (*path && *mount_root) {
        if (*path != *mount_root)
            break;

        path++;
        mount_root++;
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
vfs_findroot(const char *path, struct mount **mp, char **root)
{
    struct mount *m = nullptr;
    size_t len, max_len = 0;

    if (!path)
        return -1;

    /* Find mount point from nearest path */
    SCOPE_LOCK(mount_lock);
    for (auto&& tmp : mount_list) {
        len = count_match(path, tmp->m_path);
        if (len > max_len) {
            max_len = len;
            m = tmp;
        }
    }
    if (m == nullptr)
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
    SCOPE_LOCK(mount_lock);
    mp->m_count++;
}


/*
 * Mark a mount point as busy.
 */
void
vfs_unbusy(struct mount *mp)
{
    SCOPE_LOCK(mount_lock);
    mp->m_count--;
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

std::vector<mount_desc>
current_mounts()
{
    WITH_LOCK(mount_lock) {
        std::vector<mount_desc> ret;
        for (auto&& mp : mount_list) {
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
    SCOPE_LOCK(mount_lock);

    kprintf("mount_dump\n");
    kprintf("dev      count root\n");
    kprintf("-------- ----- --------\n");

    for (auto&& mp : mount_list) {
        kprintf("%8x %5d %s\n", mp->m_dev, mp->m_count, mp->m_path);
    }
}
#endif
