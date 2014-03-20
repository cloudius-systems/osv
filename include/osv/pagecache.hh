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

namespace pagecache {

mmu::mmupage get(vfs_file* fp, off_t offset, mmu::hw_ptep ptep, bool write, bool shared);
void release(vfs_file* fp, void *addr, off_t offset, mmu::hw_ptep ptep);

}
