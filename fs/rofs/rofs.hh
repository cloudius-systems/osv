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

//
// The Read-Only File System (ROFS) provides simple implementation of
// a file system where data from disk can only be read from and never
// written to. It is simple enough for many stateless applications
// deployed on OSv which only need to read code from local disk and never
// write to local disk. The ROFS is inspired and shares some ideas
// from the original MFS implementation by James Root from 2015.
//
// The ROFS can operate in two modes - without cache and with cache.
// Without cache ROFS reads as much data from disk as requested
// per uio passed in to the read function but it does not retain/cache it
// for any subsequent read of the same data. Conversely in cache mode
// ROFS reads more data from disk than needed per uio and stores it in memory
// in anticipation there will be more subsequent contiguous reads of data.
// By default ROFS operates in cache-mode but can be changed to non-cache
// by passing '--disable_rofs_cache' boot option.
//
// Specifically ROFS cache mode implements simple "read-around" strategy by
// dividing a file into same size (32K) segments and reading entire segment
// into memory when corresponding offset of file is requested. Files smaller
// than 32K are loaded in full on first read. This simple read-around strategy
// can achieve 80-90% cache hit ratio in many conducted measurements. Also it can
// deliver 2-3 increase of read speed over non-cache mode at some cost of
// too much unneeded data read (15-20%). Lastly the loaded data stays
// in memory forever as there is no LRU logic implemented that could limit
// memory used.
//
// The structure of the data on disk is explained in scripts/gen-rofs-img.py

#ifndef __INCLUDE_ROFS_H__
#define __INCLUDE_ROFS_H__

#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/dentry.h>
#include <osv/prex.h>
#include <osv/buf.h>

#define ROFS_VERSION            1
#define ROFS_MAGIC              0xDEADBEAD

#define ROFS_INODE_SIZE ((uint64_t)sizeof(struct rofs_inode))

#define ROFS_SUPERBLOCK_SIZE sizeof(struct rofs_super_block)
#define ROFS_SUPERBLOCK_BLOCK 0

//#define ROFS_DEBUG_ENABLED 1

#if defined(ROFS_DEBUG_ENABLED)
#define print(...) kprintf(__VA_ARGS__)
#else
#define print(...)
#endif

#define ROFS_DIAGNOSTICS_ENABLED 1

#if defined(ROFS_DIAGNOSTICS_ENABLED)
#define ROFS_STOPWATCH_START auto begin = std::chrono::high_resolution_clock::now();
#define ROFS_STOPWATCH_END(total) auto end = std::chrono::high_resolution_clock::now(); \
std::chrono::duration<double> sec = end - begin; \
total += ((long)(sec.count() * 1000000));
//TODO: Review - avoid conversions
#else
#define ROFS_STOPWATCH_START
#define ROFS_STOPWATCH_END(...)
#endif

extern struct vfsops rofs_vfsops;
extern struct vnops rofs_vnops;

struct rofs_super_block {
    uint64_t magic;
    uint64_t version;
    uint64_t block_size;
    uint64_t structure_info_first_block;
    uint64_t structure_info_blocks_count;
    uint64_t directory_entries_count;
    uint64_t symlinks_count;
    uint64_t inodes_count;
};

struct rofs_inode {
    mode_t mode;
    uint64_t inode_no;
    uint64_t data_offset;
    union {
        uint64_t file_size;
        uint64_t dir_children_count;
    };
};

struct rofs_dir_entry {
    char *filename;
    uint64_t inode_no;
};

struct rofs_info {
    struct rofs_super_block *sb;
    struct rofs_dir_entry *dir_entries;
    char **symlinks;
    struct rofs_inode *inodes;
};

namespace rofs {
    int
    cache_read(struct rofs_inode *inode, struct device *device, struct rofs_super_block *sb, struct uio *uio);
}

int rofs_read_blocks(struct device *device, uint64_t starting_block, uint64_t blocks_count, void* buf);
void rofs_set_vnode(struct vnode* vnode, struct rofs_inode *inode);

#endif
