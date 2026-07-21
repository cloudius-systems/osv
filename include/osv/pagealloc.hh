/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PAGEALLOC_HH_
#define PAGEALLOC_HH_

#include <stddef.h>

namespace memory {

void* alloc_page();
// Allocate one page preferring memory local to NUMA node `node`, falling back
// to a node-agnostic allocation when the node has no free memory or NUMA is
// unavailable.
void* alloc_page_on_node(int node);
void free_page(void* page);
void* alloc_huge_page(size_t bytes);
void free_huge_page(void *page, size_t bytes);

}

#endif /* PAGEALLOC_HH_ */
