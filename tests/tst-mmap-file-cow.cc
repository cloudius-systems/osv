/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Regression test for the readahead COW crash: a MAP_PRIVATE mapping of a
// ROFS-resident file, read sequentially to trigger readahead_if_sequential(),
// then written to.  A write fault on a page that readahead loaded into the read
// cache (but that was never faulted for read) used to hit assert(0) in
// pagecache.cc's ptep_remove; it must now COW cleanly.  Built and run on the
// ROFS test image.

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <thread>
#include <iostream>

int main()
{
    const char* path = "/rofs/mmap-file-test1";   // ~32 KiB, read-only
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0);
    size_t size = st.st_size;
    size_t page = sysconf(_SC_PAGESIZE);
    assert(size >= 4 * page);

    volatile char sink = 0;

    // MAP_PRIVATE so writes are COW.  Read the first couple of pages
    // sequentially: this makes readahead_if_sequential() speculatively load the
    // pages ahead into the read cache without any PTE mapping.
    char* p = (char*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert(p != MAP_FAILED);
    sink ^= p[0];
    sink ^= p[page];   // reading page 1 triggers readahead of pages 2..N

    // Now WRITE to a page that readahead just loaded but we never read: this is
    // the COW-on-prefetched-page path that used to assert(0).
    p[3 * page] = 0x42;
    assert(p[3 * page] == 0x42);
    // Write a few more ahead pages for good measure.
    for (size_t off = 2 * page; off + page <= size; off += page) {
        p[off] = 0x37;
        assert(p[off] == 0x37);
    }
    assert(munmap(p, size) == 0);

    // Do it again over the whole file in one sweep, mixing reads and writes.
    p = (char*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert(p != MAP_FAILED);
    for (size_t off = 0; off + page <= size; off += page) {
        sink ^= p[off];            // read (drives readahead)
        p[off + 1] = (char)off;    // write (COW, possibly on a prefetched page)
    }
    assert(munmap(p, size) == 0);

    // Concurrent read faults on the SAME file from many threads: this races
    // VOP_CACHE for the same (dev, ino, offset) keys and exercises the emplace
    // collision path in map_read_cached_page().  On ROFS the page handed to
    // that function is borrowed from the ROFS read-around cache, so a collision
    // must NOT free it -- doing so corrupted the ROFS cache and caused a
    // general-protection fault (a GraalVM app on ROFS reproduced it reliably).
    {
        const int NT = 8;
        std::thread threads[NT];
        for (int t = 0; t < NT; t++) {
            threads[t] = std::thread([&] {
                for (int rep = 0; rep < 50; rep++) {
                    char* q = (char*)mmap(nullptr, size, PROT_READ,
                                          MAP_PRIVATE, fd, 0);
                    assert(q != MAP_FAILED);
                    volatile char s = 0;
                    for (size_t off = 0; off + page <= size; off += page) {
                        s ^= q[off];
                    }
                    (void)s;
                    munmap(q, size);
                }
            });
        }
        for (int t = 0; t < NT; t++) {
            threads[t].join();
        }
    }

    close(fd);
    (void)sink;
    std::cerr << "mmap-file-cow tests PASSED\n";
    return 0;
}
