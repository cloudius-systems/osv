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

#ifndef __INCLUDE_MFS_H__
#define __INCLUDE_MFS_H__

#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/dentry.h>
#include <osv/prex.h>

#include "mfs_cache.h"

#define MFS_VERSION            1
#define MFS_MAGIC              0xDEADBEEF
#define MFS_FILENAME_MAXLEN    63
#define MFS_ROOT_INODE_NUMBER  1

#define MFS_SUPERBLOCK_SIZE (uint64_t)sizeof(struct mfs_super_block)
#define MFS_SUPERBLOCK_PADDING(bs) bs - MFS_SUPERBLOCK_SIZE
#define MFS_SUPERBLOCK_BLOCK 0
#define MFS_INODE_SIZE (uint64_t)sizeof(struct mfs_inode)
#define MFS_INODES_PER_BLOCK(bs) (bs / MFS_INODE_SIZE)
#define MFS_INODE_BLOCK_PADDING(bs) bs - (MFS_INODES_PER_BLOCK(bs) * MFS_INODE_SIZE)

#define MFS_RECORD_SIZE (uint64_t)sizeof(struct mfs_dir_record)
#define MFS_RECORDS_PER_BLOCK(bs) (bs / MFS_RECORD_SIZE)
#define MFS_RECORDS_PADDING(bs) (bs - (MFS_RECORDS_PER_BLOCK(bs) * MFS_RECORD_SIZE))

#define _MFS_RAW_BLOCKS_NEEDED(bs, c) ((c) / (bs))
#define _MFS_MOD_BLOCKS_NEEDED(bs, c) (((c) % (bs)) > 0 ? 1 : 0)
#define MFS_BLOCKS_NEEDED(bs, c) (_MFS_RAW_BLOCKS_NEEDED(bs, c) + _MFS_MOD_BLOCKS_NEEDED(bs, c))

#define MFS_RECORD_BLOCKS_NEEDED(bs, rc) (MFS_BLOCKS_NEEDED(bs, (rc * MFS_RECORD_SIZE)))
#define MFS_INODE_BLOCKS_NEEDED(bs, ic) (MFS_BLOCKS_NEEDED(bs, (ic * MFS_INODE_SIZE)))

#define MFS_INODE_BLOCK(bs, i) (i / MFS_INODES_PER_BLOCK(bs))
#define MFS_RECORD_BLOCK(bs, i) (i / MFS_RECORDS_PER_BLOCK(bs))

#define MFS_INODE_OFFSET(bs, i) (i % (MFS_INODES_PER_BLOCK(bs)))
#define MFS_RECORD_OFFSET(bs, i) (i % (MFS_RECORDS_PER_BLOCK(bs)))

#define MFS_PERMISSIONS      S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
#define MFS_FILE_PERMISSIONS S_IFREG | MFS_PERMISSIONS
#define MFS_DIR_PERMISSIONS S_IFDIR | MFS_PERMISSIONS

#define MFS_CACHE_SIZE 1024

#if 0
#define print(...) kprintf(__VA_ARGS__)
#else
#define print(...)
#endif

extern struct vfsops mfs_vfsops;
extern struct vnops mfs_vnops;

struct mfs_super_block {
    uint64_t version;
    uint64_t magic;
    uint64_t block_size;
    uint64_t inodes_block;
};


struct mfs_inode {
	mode_t   mode;
	uint64_t inode_no;
	uint64_t data_block_number;
	union {
		uint64_t file_size;
		uint64_t dir_children_count;
	};
};

struct mfs_dir_record {
    // Add one for \0
    char filename[MFS_FILENAME_MAXLEN + 1];
    uint64_t inode_no;
};

struct mfs_inode *mfs_get_inode(struct mfs *mfs, struct device *dev, uint64_t inode_no);
void              mfs_set_vnode(struct vnode* vnode, struct mfs_inode *inode);

struct mfs {
     struct mfs_super_block *sb;
     struct mfs_cache       *cache;
};

#endif

