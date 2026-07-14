/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// To compile this test on Linux, use:
// g++ -g -std=c++11 tests/tst-mremap.cc -o /tmp/tst-mremap && /tmp/tst-mremap

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include <cassert>
#include <cstdint>
#include <iostream>

static size_t page;

// Fill a region with a recognizable pattern derived from its length.
static void fill(void *p, size_t len)
{
    unsigned char *b = static_cast<unsigned char *>(p);
    for (size_t i = 0; i < len; i++) {
        b[i] = static_cast<unsigned char>((i * 31 + 7) & 0xff);
    }
}

static bool check(void *p, size_t len)
{
    unsigned char *b = static_cast<unsigned char *>(p);
    for (size_t i = 0; i < len; i++) {
        if (b[i] != static_cast<unsigned char>((i * 31 + 7) & 0xff)) {
            return false;
        }
    }
    return true;
}

// Grow an anonymous mapping.  With MREMAP_MAYMOVE it must always succeed and
// preserve the original bytes, wherever it lands.
static void test_grow_anon()
{
    void *p = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(p != MAP_FAILED);
    fill(p, page);

    void *q = mremap(p, page, 4 * page, MREMAP_MAYMOVE);
    assert(q != MAP_FAILED);
    assert(check(q, page));            // original data survived the move/grow
    // The grown tail is usable.
    fill(q, 4 * page);
    assert(check(q, 4 * page));
    assert(munmap(q, 4 * page) == 0);
}

// Shrinking never moves the mapping and never fails.
static void test_shrink_anon()
{
    void *p = mmap(nullptr, 4 * page, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(p != MAP_FAILED);
    fill(p, 4 * page);

    void *q = mremap(p, 4 * page, page, 0);  // no MAYMOVE needed for shrink
    assert(q == p);                          // address unchanged
    assert(check(q, page));                  // kept bytes intact
    assert(munmap(q, page) == 0);
}

// Growing without MREMAP_MAYMOVE must fail with ENOMEM when the mapping cannot
// be extended in place.  Here the source is the first page of a larger single
// mapping, so the bytes immediately after it are occupied and in-place growth
// is impossible.
static void test_grow_no_maymove_blocked()
{
    void *p = mmap(nullptr, 8 * page, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(p != MAP_FAILED);
    errno = 0;
    void *q = mremap(p, page, 2 * page, 0 /* no MAYMOVE */);
    assert(q == MAP_FAILED);
    assert(errno == ENOMEM);
    assert(munmap(p, 8 * page) == 0);
}

// A file-backed mapping can be grown/moved and still reflect the file bytes.
static void test_file_move()
{
    char path[] = "/tmp/tst-mremap-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    // Write two pages of pattern to the file.
    unsigned char *buf = static_cast<unsigned char *>(malloc(2 * page));
    for (size_t i = 0; i < 2 * page; i++) {
        buf[i] = static_cast<unsigned char>((i * 17 + 3) & 0xff);
    }
    assert(write(fd, buf, 2 * page) == (ssize_t)(2 * page));

    void *p = mmap(nullptr, page, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(p != MAP_FAILED);
    void *q = mremap(p, page, 2 * page, MREMAP_MAYMOVE);
    assert(q != MAP_FAILED);
    unsigned char *b = static_cast<unsigned char *>(q);
    for (size_t i = 0; i < 2 * page; i++) {
        assert(b[i] == static_cast<unsigned char>((i * 17 + 3) & 0xff));
    }
    assert(munmap(q, 2 * page) == 0);
    free(buf);
    close(fd);
    unlink(path);
}

// Error paths.
static void test_errors()
{
    void *p = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(p != MAP_FAILED);

    // new_size == 0 -> EINVAL
    errno = 0;
    assert(mremap(p, page, 0, MREMAP_MAYMOVE) == MAP_FAILED && errno == EINVAL);

    // Remapping an unmapped address -> EFAULT (single-vma requirement).
    errno = 0;
    void *bogus = static_cast<char *>(p) + 0x40000000ull;  // far, unmapped
    assert(mremap(bogus, page, 2 * page, MREMAP_MAYMOVE) == MAP_FAILED &&
           errno == EFAULT);

    assert(munmap(p, page) == 0);
}

int main()
{
    std::cerr << "Running mremap tests\n";
    page = sysconf(_SC_PAGESIZE);

    test_grow_anon();
    test_shrink_anon();
    test_grow_no_maymove_blocked();
    test_file_move();
    test_errors();

    std::cerr << "mremap tests PASSED\n";
    return 0;
}
