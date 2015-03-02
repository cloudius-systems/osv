/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CONTIGUOUS_ALLOC_HH
#define CONTIGUOUS_ALLOC_HH

namespace memory {

void* alloc_phys_contiguous_aligned(size_t sz, size_t align, bool block = true);
void free_phys_contiguous_aligned(void* p);

};

#endif /* CONTIGUOUS_ALLOC_HH */
