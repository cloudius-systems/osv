/*
 * Copyright (C) 2014 Eduardo Piva
 * Copyright (C) 2018 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "fastlz.h"
#include <string.h>
#include <cstddef>
#include <stdint.h>
#include <climits>

#define BUFFER_OUT (char *)OSV_KERNEL_BASE

#define INT_AT(byte_array,offset) (*(reinterpret_cast<int*>(byte_array + offset)))
#define SEGMENT_INFO(info_array,segment) (reinterpret_cast<int*>(info_array + (1 + 2 * segment) * sizeof(int)))
#define COPIED_SEGMENT_INFO(info_array,segment) (reinterpret_cast<int*>(info_array + 2 * segment * sizeof(int)))

extern char _binary_loader_stripped_elf_lz_start;
extern char _binary_loader_stripped_elf_lz_end;
extern char _binary_loader_stripped_elf_lz_size;

// The code in fastlz.cc does not call memset(), but some version of gcc
// implement some assignments by calling memset(), so we need to implement
// a memset() function. This is not performance-critical so let's stick to
// the basic implementation we have in libc/string/memset.c. To avoid
// compiling this source file a second time (the loader needs different
// compile parameters), we #include it here instead.
extern "C" void *memset(void *s, int c, size_t n);
#define memset_base memset
#include "libc/string/memset.c"
#undef memset_base

extern "C" void uncompress_loader()
{
    char *compressed_input = &_binary_loader_stripped_elf_lz_start;
    //
    // Read 1st four bytes to identify offset of the segments info table
    int info_offset = INT_AT(compressed_input,0);
    char *segments_info_array = compressed_input + info_offset;
    int segments_count = INT_AT(compressed_input,info_offset);
    //
    // Calculate source offset and destination offset for last segment
    size_t src_offset = sizeof(int), dst_offset = 0;
    for (auto segment = 0; segment < segments_count; segment++) {
        int *segment_info = SEGMENT_INFO(segments_info_array, segment);
        auto decomp_segment_size = *segment_info;
        auto comp_segment_size = *(segment_info + 1);
        src_offset += comp_segment_size;
        dst_offset += decomp_segment_size;
    }
    auto uncompressed_size = dst_offset;
    //
    // Copy info table to an area above target decompressed
    // kernel, otherwise we would have written over it when decompressing
    // segments
    char *copied_info_array = BUFFER_OUT + (uncompressed_size + 10);
    for (auto segment = 0; segment < segments_count; segment++) {
        int *segment_info = SEGMENT_INFO(segments_info_array, segment);
        int *target_segment_info = COPIED_SEGMENT_INFO(copied_info_array, segment);
        *target_segment_info = *segment_info;
        *(target_segment_info + 1) = *(segment_info + 1);
    }
    //
    // Iterate over segments and decompress them starting with the last one
    for (auto segment = segments_count - 1; segment >= 0; segment--) {
        int *segment_info = COPIED_SEGMENT_INFO(copied_info_array, segment);
        src_offset -= *(segment_info + 1);
        dst_offset -= *segment_info;
        if (*(segment_info + 1) < *segment_info) {
            fastlz_decompress(compressed_input + src_offset,
                              *(segment_info + 1),
                              BUFFER_OUT + dst_offset,
                              INT_MAX);
        }
        else {
            //
            // Copy from last byte to the first
            for (auto off = *segment_info - 1; off >= 0; off--)
                *(BUFFER_OUT + dst_offset + off) = *(compressed_input + src_offset + off);
        }
    }
}
