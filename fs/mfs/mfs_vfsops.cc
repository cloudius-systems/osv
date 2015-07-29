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
 */
 
#include "mfs.h"
#include <stdio.h>
#include <sys/types.h>
#include <osv/device.h>
#include <osv/debug.h>

static int mfs_mount(struct mount *mp, const char *dev, int flags, const void *data);
static int mfs_sync(struct mount *mp);
static int mfs_statfs(struct mount *mp, struct statfs *statp);
static int mfs_unmount(struct mount *mp, int flags);

#define ramfs_vget	((vfsop_vget_t)vfs_nullop)
#define ramfs_statfs	((vfsop_statfs_t)vfs_nullop)

struct vfsops mfs_vfsops = {
	mfs_mount,		            /* mount */
	mfs_unmount,		        /* unmount */
	mfs_sync,		            /* sync */
	((vfsop_vget_t)vfs_nullop), /* vget */
	mfs_statfs,		            /* statfs */
	&mfs_vnops,		            /* vnops */
};

static int
mfs_mount(struct mount *mp, const char *dev, int flags, const void *data) {
    struct device          *device;
    struct mfs_buf         *bh    = NULL;;
    struct mfs             *mfs   = NULL;
    struct mfs_super_block *sb    = NULL;
    struct mfs_cache       *cache = NULL;
    struct mfs_inode *root_inode  = NULL;
    int error = -1;

    print("[mfs] Mounting %s\n", dev);
    print("[mfs]    dev = %s\n", dev);
    print("[mfs]    flags = %d\n", flags);

    error = device_open(dev + 5, DO_RDWR, &device);
    if (error) {
        kprintf("[mfs] Error opening device!\n");
        return error;
    }

    cache = new mfs_cache(MFS_CACHE_SIZE);

    error = cache->read(device, MFS_SUPERBLOCK_BLOCK, &bh);
    if (error) {
        kprintf("[mfs] Error reading mfs superblock\n");
        delete cache;
        return error;
    }

    sb = new mfs_super_block;
    memcpy(sb, bh->data, sizeof(struct mfs_super_block));
    cache->release(bh);

    // kprintf("[mfs] super block: %p\n", mfs);

    if (sb->magic != MFS_MAGIC) {
        print("[mfs] Error magics do not match!\n");
        print("[mfs] Expecting %016llX but got %016llX\n", MFS_MAGIC, sb->magic);
        delete cache;
        return -1; // TODO: Proper error code
    }
    print("[mfs] Got superblock version: 0x%016llX\n", sb->version);
    print("[mfs] Got magic:              0x%016llX\n", sb->magic);
    print("[mfs] Got block size:         0x%016llX\n", sb->block_size);
    print("[mfs] Got inode block:        0x%016llX\n", sb->inodes_block);
    
    mfs = new struct mfs;
    
    mfs->sb    = sb;
    mfs->cache = cache;

    // Save a reference to our superblock
    mp->m_data = mfs;
    mp->m_dev = device;

    root_inode = mfs_get_inode(mfs, device, MFS_ROOT_INODE_NUMBER);

    mfs_set_vnode(mp->m_root->d_vnode, root_inode);

    return 0;
}

static int mfs_sync(struct mount *mp) {
    return 0;
}

static int mfs_statfs(struct mount *mp, struct statfs *statp) {
    struct mfs             *mfs = (struct mfs*)mp->m_data;
    struct mfs_super_block *sb  = mfs->sb;;

    statp->f_bsize = sb->block_size;

    // Total blocks, unknown...
    statp->f_blocks = sb->inodes_block;
    // Read only. 0 blocks free
    statp->f_bfree = 0;
    statp->f_bavail = 0;

    statp->f_ffree = 0;
    statp->f_files = sb->inodes_block; //Needs to be inode count

    statp->f_namelen = MFS_FILENAME_MAXLEN;

    return 0;
}

static int
mfs_unmount(struct mount *mp, int flags) {
    // kprintf("[mfs] mfs_umount called: %d\n", flags);
    return 0;
}
