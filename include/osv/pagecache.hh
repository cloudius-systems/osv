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

bool get(vfs_file* fp, off_t offset, mmu::hw_ptep ptep, mmu::pt_element pte, bool write, bool shared);
bool release(vfs_file* fp, void *addr, off_t offset, mmu::hw_ptep ptep);
void unmap_arc_buf(arc_buf_t* ab);

}
