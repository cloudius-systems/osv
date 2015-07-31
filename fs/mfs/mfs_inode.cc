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
 * Released under a modified BSD license. For full terms, please see the
 * license file in this folder or contact permission@sei.cmu.edu.
 *
 * DM-0002621
 *
 * Based on https://github.com/jdroot/mfs
 */

#include "mfs.hh"

#include <stdio.h>
#include <sys/types.h>
#include <osv/device.h>
#include <osv/buf.h>
#include <osv/debug.h>

struct mfs_inode *mfs_get_inode(struct mfs *mfs, struct device *dev, uint64_t inode_no) {
    struct mfs_super_block *sb    = mfs->sb;
    struct mfs_inode       *inode = nullptr;
    struct mfs_inode       *rv    = nullptr;
    struct buf             *bh    = nullptr;
    
    uint64_t i            = inode_no - 1;
    int      error        = -1;
    uint64_t inode_block  = sb->inodes_block;
    uint64_t inode_offset = 0;

    inode_block += MFS_INODE_BLOCK(sb->block_size, i);
    inode_offset = MFS_INODE_OFFSET(sb->block_size, i);

    print("[mfs] looking for inode %llu in block %llu\n", inode_no, inode_block);

    error = mfs_cache_read(mfs, dev, inode_block, &bh);
    if (error) {
        kprintf("[mfs] Error reading block [%llu]\n", inode_block);
        return nullptr;
    }

    inode = (struct mfs_inode *)bh->b_data;
    inode += inode_offset;

    print("[mfs] got inode_no = %llu\n", inode->inode_no);

    // Assert is somewhat dangerous here, but if this assert fails the filesystem
    // has been corrupted somehow.
    assert(inode->inode_no == inode_no);

    rv = new mfs_inode;
    memcpy(rv, inode, sizeof(struct mfs_inode));

    mfs_cache_release(mfs, bh);

    return rv;
}

void mfs_set_vnode(struct vnode* vnode, struct mfs_inode *inode) {
    off_t size = 0;
    if (vnode == nullptr || inode == nullptr) {
        return;
    }

    vnode->v_data = inode;
    vnode->v_ino = inode->inode_no;

    // Set type
    if (S_ISDIR(inode->mode)) {
        size = MFS_INODE_SIZE;
        vnode->v_type = VDIR;
    } else if (S_ISREG(inode->mode)) {
        size = inode->file_size;
        vnode->v_type = VREG;
    } else if (S_ISLNK(inode->mode)) {
        size = 512; // Max size
        vnode->v_type = VLNK;
    }

    vnode->v_mode = inode->mode;
    vnode->v_size = size;
}

