/*
 * Copyright (C) 2014 Eduardo Piva
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "fastlz.h"
#include <cstddef>
#include <stdint.h>

#define BUFFER_OUT (char *)0x200000
#define MAX_BUFFER 0x1600000

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
    fastlz_decompress(&_binary_loader_stripped_elf_lz_start,
            (size_t) &_binary_loader_stripped_elf_lz_size,
            BUFFER_OUT, MAX_BUFFER);
}
