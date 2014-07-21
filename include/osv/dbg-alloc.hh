/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DBG_ALLOC_HH_
#define DBG_ALLOC_HH_

#include <stdint.h>
#include <cstddef>

namespace dbg {

void* malloc(size_t size, size_t align = alignof(max_align_t));
void free(void* object);

/// new/delete overloads for using the debug allocator for a type
///
/// To use for my_class, change its definition as follows:
///
/// class my_class : public tracked<my_class> { ... };
///
/// this will cause allocations and deallocations of my_class to be
/// monitored for use-after-free and double-frees.
template <typename T>
struct tracked {
    void* operator new(size_t sz) { return dbg::malloc(sz); }
    void* operator new(size_t sz, std::nothrow_t) { return dbg::malloc(sz); }
    void operator delete(void* obj) { ::free(obj); }
};

}



#endif /* DBG_ALLOC_HH_ */
