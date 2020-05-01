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

#include "rofs.hh"
#include <sys/types.h>
#include <osv/device.h>
#include <osv/debug.h>
#include <fs/vfs/vfs_id.h>
#include <iomanip>
#include <iostream>

static int rofs_mount(struct mount *mp, const char *dev, int flags, const void *data);
static int rofs_sync(struct mount *mp);
static int rofs_statfs(struct mount *mp, struct statfs *statp);
static int rofs_unmount(struct mount *mp, int flags);

#define rofs_vget ((vfsop_vget_t)vfs_nullop)

#if defined(ROFS_DIAGNOSTICS_ENABLED)
std::atomic<long> rofs_block_read_ms(0);
std::atomic<long> rofs_block_read_count(0);
std::atomic<long> rofs_block_allocated(0);
std::atomic<long> rofs_cache_reads(0);
std::atomic<long> rofs_cache_misses(0);
#endif

std::atomic<long> rofs_mounts(0);

struct vfsops rofs_vfsops = {
    rofs_mount,		/* mount */
    rofs_unmount,	/* unmount */
    rofs_sync,		/* sync */
    rofs_vget,      /* vget */
    rofs_statfs,	/* statfs */
    &rofs_vnops	    /* vnops */
};

static int
rofs_mount(struct mount *mp, const char *dev, int flags, const void *data)
{
    struct device *device;
    struct rofs_info *rofs = nullptr;
    struct rofs_super_block *sb = nullptr;
    int error = -1;

    error = device_open(dev + 5, DO_RDWR, &device);
    if (error) {
        kprintf("[rofs] Error opening device!\n");
        return error;
    }

    void *buf = malloc(BSIZE); //Just enough for single block of 512 bytes
    error = rofs_read_blocks(device, ROFS_SUPERBLOCK_BLOCK, 1, buf);
    if (error) {
        kprintf("[rofs] Error reading rofs superblock\n");
        device_close(device);
        free(buf);
        return error;
    }

    // We see if the file system is ROFS, if not, return error and close everything
    sb = (struct rofs_super_block *) buf;
    if (sb->magic != ROFS_MAGIC) {
        print("[rofs] Error magics do not match!\n");
        print("[rofs] Expecting %016llX but got %016llX\n", ROFS_MAGIC, sb->magic);
        free(buf);
        device_close(device);
        return -1; // TODO: Proper error code
    }

    if (sb->version != ROFS_VERSION) {
        kprintf("[rofs] Found rofs volume but incompatible version!\n");
        kprintf("[rofs] Expecting %llu but found %llu\n", ROFS_VERSION, sb->version);
        free(buf);
        device_close(device);
        return -1;
    }

    print("[rofs] Got superblock version:   0x%016llX\n", sb->version);
    print("[rofs] Got magic:                0x%016llX\n", sb->magic);
    print("[rofs] Got block size:                  %d\n", sb->block_size);
    print("[rofs] Got structure info first block:  %d\n", sb->structure_info_first_block);
    print("[rofs] Got structure info blocks count: %d\n", sb->structure_info_blocks_count);
    print("[rofs] Got directory entries count:     %d\n", sb->directory_entries_count);
    print("[rofs] Got symlinks count:              %d\n", sb->symlinks_count);
    print("[rofs] Got inode count:                 %d\n", sb->inodes_count);
    //
    // Since we have found ROFS, we can copy the superblock now
    sb = new rofs_super_block;
    memcpy(sb, buf, ROFS_SUPERBLOCK_SIZE);
    free(buf);
    //
    // Read structure_info_blocks_count to construct array of directory enries, symlinks and i-nodes
    buf = malloc(BSIZE * sb->structure_info_blocks_count);
    error = rofs_read_blocks(device, sb->structure_info_first_block, sb->structure_info_blocks_count, buf);
    if (error) {
        kprintf("[rofs] Error reading rofs structure info blocks\n");
        device_close(device);
        free(buf);
        return error;
    }

    rofs = new struct rofs_info;
    rofs->sb = sb;
    rofs->dir_entries = (struct rofs_dir_entry *) malloc(sizeof(struct rofs_dir_entry) * sb->directory_entries_count);

    void *data_ptr = buf;
    //
    // Read directory entries
    for (unsigned int idx = 0; idx < sb->directory_entries_count; idx++) {
        struct rofs_dir_entry *dir_entry = &(rofs->dir_entries[idx]);
        dir_entry->inode_no = *((uint64_t *) data_ptr);
        data_ptr += sizeof(uint64_t);

        unsigned short *filename_size = (unsigned short *) data_ptr;
        data_ptr += sizeof(unsigned short);

        dir_entry->filename = (char *) malloc(*filename_size + 1);
        strncpy(dir_entry->filename, (char *) data_ptr, *filename_size);
        dir_entry->filename[*filename_size] = 0;
        print("[rofs] i-node: %d -> directory entry: %s\n", dir_entry->inode_no, dir_entry->filename);
        data_ptr += *filename_size * sizeof(char);
    }
    //
    // Read symbolic links
    rofs->symlinks = (char **) malloc(sizeof(char *) * sb->symlinks_count);

    for (unsigned int idx = 0; idx < sb->symlinks_count; idx++) {
        unsigned short *symlink_path_size = (unsigned short *) data_ptr;
        data_ptr += sizeof(unsigned short);

        rofs->symlinks[idx] = (char *) malloc(*symlink_path_size + 1);
        strncpy(rofs->symlinks[idx], (char *) data_ptr, *symlink_path_size);
        rofs->symlinks[idx][*symlink_path_size] = 0;
        print("[rofs] symlink: %s\n", rofs->symlinks[idx]);
        data_ptr += *symlink_path_size * sizeof(char);
    }
    //
    // Read i-nodes
    rofs->inodes = (struct rofs_inode *) malloc(sizeof(struct rofs_inode) * sb->inodes_count);
    memcpy(rofs->inodes, data_ptr, sb->inodes_count * sizeof(struct rofs_inode));

    for (unsigned int idx = 0; idx < sb->inodes_count; idx++) {
        print("[rofs] inode: %d, size: %d\n", rofs->inodes[idx].inode_no, rofs->inodes[idx].file_size);
    }

    free(buf);

    // Save a reference to our superblock
    mp->m_data = rofs;
    mp->m_dev = device;

    rofs_mounts += 1;
    mp->m_fsid.__val[0] = rofs_mounts.load();
    mp->m_fsid.__val[1] = ROFS_ID >> 32;

    rofs_set_vnode(mp->m_root->d_vnode, rofs->inodes);

    print("[rofs] returning from mount\n");

    return 0;
}

static int rofs_sync(struct mount *mp) {
    return 0;
}

static int rofs_statfs(struct mount *mp, struct statfs *statp)
{
    struct rofs_info *rofs = (struct rofs_info *) mp->m_data;
    struct rofs_super_block *sb = rofs->sb;

    statp->f_bsize = sb->block_size;

    // Total blocks
    statp->f_blocks = sb->structure_info_blocks_count + sb->structure_info_first_block;
    // Read only. 0 blocks free
    statp->f_bfree = 0;
    statp->f_bavail = 0;

    statp->f_ffree = 0;
    statp->f_files = sb->inodes_count; //Needs to be inode count

    statp->f_namelen = 0; //FIXME - unlimited ROFS_FILENAME_MAXLEN;

    statp->f_fsid.__val[0] = mp->m_fsid.__val[0];
    statp->f_fsid.__val[1] = mp->m_fsid.__val[1];

    return 0;
}

static int
rofs_unmount(struct mount *mp, int flags)
{
    struct rofs_info *rofs = (struct rofs_info *) mp->m_data;
    struct rofs_super_block *sb = rofs->sb;
    struct device *dev = mp->m_dev;

    int error = device_close(dev);
    delete sb;
    delete rofs;

#if defined(ROFS_DIAGNOSTICS_ENABLED)
    debugf("ROFS: spent %.2f ms reading from disk\n", ((double) rofs_block_read_ms.load()) / 1000);
    debugf("ROFS: read %d 512-byte blocks from disk\n", rofs_block_read_count.load());
    debugf("ROFS: allocated %d 512-byte blocks of cache memory\n", rofs_block_allocated.load());
    long total_cache_reads = rofs_cache_reads.load();
    double hit_ratio = total_cache_reads > 0 ? (rofs_cache_reads.load() - rofs_cache_misses.load()) / ((double)total_cache_reads) : 0;
    debugf("ROFS: hit ratio is %.2f%%\n", hit_ratio * 100);
#endif
    return error;
}
