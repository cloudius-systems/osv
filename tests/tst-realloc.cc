/*
* Copyright (C) 2020 Waldemar Kozaczuk
*
* This work is open source software, licensed under the terms of the
* BSD license as described in the LICENSE file in the top-level directory.
*/

#include <stdlib.h>
#include <string.h>
#include <cassert>
#include <iostream>

#ifdef __OSV__
#include <osv/kernel_config_fork.h>
#if CONF_fork
#include <osv/fork_arena.hh>
#endif
#endif

extern "C" size_t malloc_usable_size (void *ptr);

static void test_realloc(size_t original_size, size_t new_size)
{
    char data[11] = "0123456789";

    void *original_buf = malloc(original_size);
    assert(original_buf);

    char *buf = static_cast<char*>(original_buf);
    for (size_t i = 0; i < original_size; i++) {
        buf[i] = data[i % 10];
    }

    void *new_buf = realloc(original_buf, new_size);
    assert(new_buf);

    auto expected_same_data_len = std::min(original_size, new_size);
    buf = static_cast<char*>(new_buf);
    for (size_t i = 0; i < expected_same_data_len; i++) {
        assert(buf[i] == data[i % 10]);
    }

    free(new_buf);

    std::cerr << "PASSED realloc() for original_size: " << original_size << ", new_size: " << new_size << std::endl;
}

#ifdef __OSV__
// The expected values are the mempool allocator's size classes. With
// CONF_fork, an application allocation of up to 2 MiB comes from the fork
// arena instead, whose classes differ, so for those only the generic
// contract is checked: at least size bytes, all of them writable.
static void test_usable_size(size_t size, size_t expected_usable_size)
{
    void* ptr = malloc(size);
    assert(ptr);
    size_t usable = malloc_usable_size(ptr);
    assert(usable >= size);
    memset(ptr, 0x5a, usable);
#if CONF_fork
    if (fork_arena::contains(ptr)) {
        free(ptr);
        std::cerr << "PASSED malloc_usable_size() for size: " << size
                  << " (fork arena, usable " << usable
                  << "; mempool class not checked)" << std::endl;
        return;
    }
#endif
    assert(expected_usable_size == usable);
    free(ptr);

    std::cerr << "PASSED malloc_usable_size() for size: " << size << std::endl;
}
#endif

int main()
{
    test_realloc(1,2);
    test_realloc(2,1);

    test_realloc(4,7);
    test_realloc(7,4);

    test_realloc(63,128);
    test_realloc(128,63);

    test_realloc(4000,5000);
    test_realloc(5000,4000);

    test_realloc(4096,4096);

    test_realloc(0x100000,0x100000);
    test_realloc(0x100000,0x100900);
    test_realloc(0x100900,0x100000);

    test_realloc(0x200000,0x200000);
    test_realloc(0x200000,0x300900);
    test_realloc(0x300900,0x200000);

    test_realloc(0x600900,0x600000);
    test_realloc(0x400000,0x600000);
    test_realloc(0x600000,0x400900);

    void *buf = realloc(nullptr, 0);
    assert(buf);
    free(buf);

    buf = malloc(16);
    assert(!realloc(buf, 0));

#ifdef __OSV__
    test_usable_size(1, 8);
    test_usable_size(8, 8);
    test_usable_size(67, 128);
    test_usable_size(0x4010, 0x4FC0);
    test_usable_size(0x100000, 0x100FC0);
    test_usable_size(0x200000, 0x200FC0);
#endif

    std::cerr << "PASSED\n";
    return 0;
}
