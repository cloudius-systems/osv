/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises the ext4 (libext) fsync durability fix and the vop_cache pagecache
// bridge.  Boot with an ext4 second disk mounted at /data, e.g.:
//
//   scripts/run.py --second-disk-image ./ext_images/ext4.img \
//     --execute='--mount-fs=ext,/dev/vblk1,/data tests/tst-ext4-rw.so'
//
// The disk is expected to contain /data/readme.dat: 12288 bytes of the pattern
// (i*7+3)&0xff.

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <cassert>
#include <cstring>
#include <string>
#include <iostream>

static const char *DATA = "/data";

int main()
{
    std::cerr << "Running ext4-rw tests\n";

    // ---- vop_cache bridge: mmap a pre-populated ext4 file and verify it ----
    std::string readme = std::string(DATA) + "/readme.dat";
    int fd = open(readme.c_str(), O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0);
    const size_t N = 12288;   // 3 pages, known pattern
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

    // ---- fsync durability: write a file, fsync it, read it back ----
    std::string out = std::string(DATA) + "/written.dat";
    fd = open(out.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
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
    fd = open(out.c_str(), O_RDONLY);
    assert(fd >= 0);
    assert(pread(fd, &check[0], W, 0) == (ssize_t)W);
    assert(check == data);
    close(fd);
    unlink(out.c_str());

    std::cerr << "ext4-rw tests PASSED\n";
    return 0;
}
