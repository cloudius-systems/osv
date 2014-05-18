/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/file.h>
#include <osv/vfs_file.hh>
#include <osv/mmu.hh>
#include "arch-mmu.hh"

struct arc_buf;
typedef arc_buf arc_buf_t;

namespace pagecache {

struct hashkey {
    dev_t dev;
    ino_t ino;
    off_t offset;
    bool operator==(const hashkey& a) const noexcept {
        return (dev == a.dev) && (ino == a.ino) && (offset == a.offset);
    }
};

struct arc_hashkey {
    uint64_t key[4];
    bool operator==(const arc_hashkey& a) const noexcept {
        return memcmp(key, a.key, sizeof(key));
    }
};

bool get(vfs_file* fp, off_t offset, mmu::hw_ptep ptep, mmu::pt_element pte, bool write, bool shared);
bool release(vfs_file* fp, void *addr, off_t offset, mmu::hw_ptep ptep);
void unmap_arc_buf(arc_buf_t* ab);
void map_arc_buf(hashkey* key, arc_buf_t* ab, void* page);
}
