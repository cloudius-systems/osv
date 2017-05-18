/*
 * Copyright (C) 2014 Eduardo Piva
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "fastlz.h"
#include <cstddef>
#include <stdint.h>
#include <climits>

#define BUFFER_OUT (char *)OSV_KERNEL_BASE

extern char _binary_loader_stripped_elf_lz_start;
extern char _binary_loader_stripped_elf_lz_end;
extern char _binary_loader_stripped_elf_lz_size;

// std libraries used by fastlz.
extern "C" void *memset(void *s, int c, size_t n)
{
    return __builtin_memset(s, c, n);
}

extern "C" void uncompress_loader()
{
    // We do not know the exact uncompressed size, so don't really want to
    // pass a the last (maxout) parameter of fastlz_decompress. Let it
    // uncompress as much as it has input. The Makefile already verifies
    // that the uncompressed kernel doesn't overwrite this uncompression code.
    // Sadly, "INT_MAX" is the largest number we can pass. If we ever need
    // more than 2GB here, it won't work.
    fastlz_decompress(&_binary_loader_stripped_elf_lz_start,
            (size_t) &_binary_loader_stripped_elf_lz_size,
            BUFFER_OUT, INT_MAX);
}
