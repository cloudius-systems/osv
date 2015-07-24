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
#include <osv/buf.h>
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
    struct device *device;
    struct buf *bh = NULL;;
    struct mfs_super_block *mfs = NULL;
    struct mfs_super_block *ret = NULL;
    struct mfs_inode *root_inode = NULL;
    int error = -1;

    print("[mfs] Mounting %s\n", dev);

    print("[mfs] mfs_mount called:\n");
    print("[mfs]    dev = %s\n", dev);
    print("[mfs]    flags = %d\n", flags);

    error = device_open(dev + 5, DO_RDWR, &device);
    if (error) {
        kprintf("[mfs] Error opening device!\n");
        return error;
    }

    // kprintf("[mfs] Successfully opened %s\n", dev);

    // kprintf("[mfs] Trying to read from device...\n");

    error = bread(device, MFS_SUPERBLOCK_BLOCK, &bh);

    if (error) {
        kprintf("[mfs] Error reading mfs superblock\n");
        return error;
    }

    mfs = (struct mfs_super_block*) bh->b_data;

    // kprintf("[mfs] super block: %p\n", mfs);

    if (mfs) {
        if (mfs->magic != MFS_MAGIC) {
            print("[mfs] Error magics do not match!\n");
            print("[mfs] Expecting %016llX but got %016llX\n", MFS_MAGIC, mfs->magic);
            brelse(bh);
            return -1; // TODO: Proper error code
        }
        print("[mfs] Got superblock version: 0x%016llX\n", mfs->version);
        print("[mfs] Got magic:              0x%016llX\n", mfs->magic);
        print("[mfs] Got block size:         0x%016llX\n", mfs->block_size);
        print("[mfs] Got inode block:        0x%016llX\n", mfs->inodes_block);
        
        ret = new mfs_super_block; // No need for kernel memory
        ret->version      = mfs->version;
        ret->magic        = mfs->magic;
        ret->block_size   = mfs->block_size;
        ret->inodes_block = mfs->inodes_block;

        // Save a reference to our superblock
        mp->m_data = ret;
        mp->m_dev = device;
        // mp->m_fsid = mfs->magic;
    } else {
        brelse(bh);
        return -1;
    }

    brelse(bh);


    root_inode = mfs_get_inode(ret, device, MFS_ROOT_INODE_NUMBER);

    mfs_set_vnode(mp->m_root->d_vnode, root_inode);

    // kprintf("[mfs] leaving mfs_mount\n");

    return 0;
}

static int mfs_sync(struct mount *mp) {
    // kprintf("[mfs] mfs_sync called: TODO\n");
    return 0;
}

static int mfs_statfs(struct mount *mp, struct statfs *statp) {
    // kprintf("[mfs] mfs_statfs called\n");
    struct mfs_super_block *mfs = (struct mfs_super_block *)mp->m_data;

    statp->f_bsize = mfs->block_size;

    // Total blocks, unknown...
    statp->f_blocks = mfs->inodes_block;
    // Read only. 0 blocks free
    statp->f_bfree = 0;
    statp->f_bavail = 0;

    statp->f_ffree = 0;
    statp->f_files = mfs->inodes_block; //Needs to be inode count

    statp->f_namelen = MFS_FILENAME_MAXLEN;

    // statp->f_fsid = mfs->magic; /* File system identifier */

    return 0;
}

static int
mfs_unmount(struct mount *mp, int flags) {
    // kprintf("[mfs] mfs_umount called: %d\n", flags);
    return 0;
}
