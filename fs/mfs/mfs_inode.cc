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

struct mfs_inode *mfs_get_inode(struct mfs *mfs, struct device *dev, uint64_t inode_no) {
    struct mfs_super_block *sb    = mfs->sb;
    struct mfs_cache       *cache = mfs->cache;
    struct mfs_inode       *inode = NULL;
    struct mfs_inode       *rv    = NULL;
    struct mfs_buf         *bh    = NULL;
    
    uint64_t i            = inode_no - 1;
    int      error        = -1;
    uint64_t inode_block  = sb->inodes_block;
    uint64_t inode_offset = 0;

    inode_block += MFS_INODE_BLOCK(sb->block_size, i);
    inode_offset = MFS_INODE_OFFSET(sb->block_size, i);

    print("[mfs] looking for inode %llu in block %llu\n", inode_no, inode_block);

    error = cache->read(dev, inode_block, &bh);
    if (error) {
        kprintf("[mfs] Error reading block [%llu]\n", inode_block);
        cache->release(bh);
        return NULL;
    }

    inode = (struct mfs_inode *)bh->data;
    inode += inode_offset;

    print("[mfs] got inode_no = %llu\n", inode->inode_no);

    assert(inode->inode_no == inode_no);

    rv = new mfs_inode;
    memcpy(rv, inode, sizeof(struct mfs_inode));

    cache->release(bh);

    return rv;
}

void mfs_set_vnode(struct vnode* vnode, struct mfs_inode *inode) {
    off_t size = 0;
    if (vnode == NULL || inode == NULL) {
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
    }

    vnode->v_mode = inode->mode;
    vnode->v_size = size;
}

