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

#ifndef _RAMFS_H
#define _RAMFS_H

#include <osv/prex.h>
#include <map>

/* #define DEBUG_RAMFS 1 */

#ifdef DEBUG_RAMFS
#define DPRINTF(a)	dprintf a
#else
#define DPRINTF(a)    do {} while (0)
#endif

#define ASSERT(e)    assert(e)

struct ramfs_file_segment {
    size_t size;
    char *data;
};

/*
 * File/directory node for RAMFS
 */
struct ramfs_node {
    struct ramfs_node *rn_next;   /* next node in the same directory */
    struct ramfs_node *rn_child;  /* first child node */
    int rn_type;    /* file or directory */
    char *rn_name;    /* name (null-terminated) */
    size_t rn_namelen;    /* length of name not including terminator */
    size_t rn_size;    /* file size */
    uint64_t inode_no;

    /* Holds data for both symlinks and regular files.
     * Each ramfs_file_segment holds single chunk of file and is keyed
     * in the map by its offset in that file; the first entry will have a key 0
     * and hold the very first chunk of the file. This way as file grows
     * we do not need to free old and allocate new memory buffer, instead
     * we only allocate new file segment for new chunk of the file and add
     * it to the map */
    std::map<off_t,struct ramfs_file_segment> *rn_file_segments_by_offset;
    /* Sum of sizes of all segments - typically bigger than actual file size
     * We could have iterated over all entries in the map to calculate it
     * but it is faster to cache it as a field */
    size_t rn_total_segments_size;

    struct timespec rn_ctime;
    struct timespec rn_atime;
    struct timespec rn_mtime;

    int rn_mode;
    bool rn_owns_buf;
    int rn_ref_count;
    bool rn_removed;
};

struct ramfs_node *ramfs_allocate_node(const char *name, int type);

void ramfs_free_node(struct ramfs_node *node);

#endif /* !_RAMFS_H */
