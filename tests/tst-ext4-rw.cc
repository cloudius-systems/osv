/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises the ext4 (libext) fsync durability fix and the vop_cache pagecache
// bridge.  This is an ext-specific test: build a test image with ext as the
// root filesystem and run it there, e.g.:
//
//   scripts/build image=tests fs=ext
//   scripts/test.py
//
// It creates its own fixture on the ext root, so it needs no pre-populated
// disk.

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <osv/mempool.hh>

#include <cassert>
#include <cstring>
#include <string>
#include <iostream>

// The ext root filesystem is writable, so create the fixture there.
static const char *READScratch = "/tst-ext4-readme.dat";
static const char *WRITEScratch = "/tst-ext4-written.dat";

// Write N bytes of a known pattern to path and fsync it, so the mmap/read
// checks below have a real ext file to exercise the vop_cache bridge against.
static void make_fixture(const char *path, size_t n)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    assert(fd >= 0);
    std::string buf(n, 0);
    for (size_t i = 0; i < n; i++) {
        buf[i] = (char)((i * 7 + 3) & 0xff);
    }
    assert(write(fd, buf.data(), n) == (ssize_t)n);
    assert(fsync(fd) == 0);
    close(fd);
}

int main()
{
    std::cerr << "Running ext4-rw tests\n";

    // ---- vop_cache bridge: mmap a self-created ext4 file and verify it ----
    const size_t N = 12288;   // 3 pages, known pattern
    make_fixture(READScratch, N);

    int fd = open(READScratch, O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0);
    assert((size_t)st.st_size == N);

    // MAP_SHARED read mmap: the fault path calls VOP_CACHE (ext_map_cached_page)
    // to warm the page cache, then serves the page.  Read all three pages and
    // verify the known pattern survives the bridge intact.
    unsigned char *p = (unsigned char *)mmap(nullptr, N, PROT_READ, MAP_SHARED, fd, 0);
    assert(p != MAP_FAILED);
    for (size_t i = 0; i < N; i++) {
        assert(p[i] == (unsigned char)((i * 7 + 3) & 0xff));
    }
    // Read again (now served from the page cache) - still correct.
    for (size_t i = 0; i < N; i += 512) {
        assert(p[i] == (unsigned char)((i * 7 + 3) & 0xff));
    }
    assert(munmap(p, N) == 0);
    close(fd);

    // ---- page-cache leak check: the ext vop_cache path allocates a fresh
    // page per cached page and hands it to the page cache, which must free it
    // when the mapping is torn down (unlike ROFS/ZFS, which pass a borrowed
    // page).  Map and unmap the file many times and confirm free memory does
    // not trend downward: a per-cycle leak of the 3 cached pages would show as
    // a steady multi-page drop across the loop.
    {
        fd = open(READScratch, O_RDONLY);
        assert(fd >= 0);
        // Warm once and settle any first-touch allocations before sampling.
        for (int w = 0; w < 3; w++) {
            unsigned char *q = (unsigned char *)mmap(nullptr, N, PROT_READ,
                                                     MAP_SHARED, fd, 0);
            assert(q != MAP_FAILED);
            volatile unsigned char sink = 0;
            for (size_t i = 0; i < N; i += 4096) sink ^= q[i];
            (void)sink;
            assert(munmap(q, N) == 0);
        }
        size_t free_before = memory::stats::free();
        const int CYCLES = 200;
        for (int c = 0; c < CYCLES; c++) {
            unsigned char *q = (unsigned char *)mmap(nullptr, N, PROT_READ,
                                                     MAP_SHARED, fd, 0);
            assert(q != MAP_FAILED);
            volatile unsigned char sink = 0;
            for (size_t i = 0; i < N; i += 4096) sink ^= q[i];
            (void)sink;
            assert(munmap(q, N) == 0);
        }
        size_t free_after = memory::stats::free();
        close(fd);
        // Before the fix this leaks ~3 pages/cycle (200*3*4096 ~= 2.4 MB).
        // Allow generous slack for unrelated allocator noise but well under a
        // real leak.
        ssize_t dropped = (ssize_t)free_before - (ssize_t)free_after;
        std::cerr << "ext4 mmap leak check: " << CYCLES << " cycles, free "
                  << free_before << " -> " << free_after << " (dropped "
                  << dropped << " bytes)\n";
        assert(dropped < (ssize_t)(CYCLES * 4096));
    }

    unlink(READScratch);

    // ---- fsync durability: write a file, fsync it, read it back ----
    fd = open(WRITEScratch, O_CREAT | O_TRUNC | O_RDWR, 0644);
    assert(fd >= 0);
    const size_t W = 8000;
    std::string data(W, 0);
    for (size_t i = 0; i < W; i++) {
        data[i] = (char)((i * 11 + 5) & 0xff);
    }
    assert(write(fd, data.data(), W) == (ssize_t)W);
    // fsync must now be a real flush (was a no-op before this change).  It must
    // return 0 and not error.
    assert(fsync(fd) == 0);
    // Read it back and verify.
    std::string check(W, 0);
    assert(pread(fd, &check[0], W, 0) == (ssize_t)W);
    assert(check == data);
    // fdatasync also works.
    assert(write(fd, data.data(), 100) == 100);
    assert(fdatasync(fd) == 0);
    close(fd);

    // Re-open and verify the fsync'd data is present.
    fd = open(WRITEScratch, O_RDONLY);
    assert(fd >= 0);
    assert(pread(fd, &check[0], W, 0) == (ssize_t)W);
    assert(check == data);
    close(fd);
    unlink(WRITEScratch);

    std::cerr << "ext4-rw tests PASSED\n";
    return 0;
}
