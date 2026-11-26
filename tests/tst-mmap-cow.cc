/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A copy-on-write fault in one private mapping of a file must not affect
// another mapping of the same file pages. The page cache tracks which ptes
// map each cached page, and used to forget the other mapping's pte when the
// COW fault removed its own (not even present) mapping. The page was then
// dropped from the cache while still mapped: on ext, whose cached pages the
// page cache owns, the page was freed, and on ZFS its ARC buffer was
// released. The other mapping then saw whatever reused the page, and
// unmapping it later freed the page again. This is what an ELF loader does:
// a library's code and data segments are two private mappings that usually
// share the file page at their boundary, and relocations are written into the
// data segment.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

static constexpr size_t page = 4096;
static constexpr size_t npages = 16;
static constexpr size_t file_size = page * npages;
static const char *path = "/tmp/tst-mmap-cow";

static unsigned char expected(size_t off)
{
    return (unsigned char)(off * 29 + (off >> 12) * 101 + 7);
}

// Allocate and touch memory, so freed pages get reused.
static void memory_pressure()
{
    std::vector<void*> blobs;
    for (int i = 0; i < 32; i++) {
        void *b = malloc(1 << 20);
        memset(b, 0xee, 1 << 20);
        blobs.push_back(b);
    }
    for (auto b : blobs) {
        free(b);
    }
}

int main()
{
    std::vector<unsigned char> buf(file_size);
    for (size_t i = 0; i < file_size; i++) {
        buf[i] = expected(i);
    }
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0 || write(fd, buf.data(), file_size) != (ssize_t)file_size || fsync(fd) != 0) {
        printf("FAIL: could not create %s\n", path);
        return 1;
    }
    close(fd);

    int fails = 0;
    for (int round = 0; round < 20; round++) {
        fd = open(path, O_RDONLY);
        // Mapping A: read every page, so A maps the cached pages.
        auto a = static_cast<unsigned char *>(mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
        volatile unsigned char sum = 0;
        for (size_t i = 0; i < file_size; i += page) {
            sum += a[i];
        }
        // Mapping B of the same pages: the first access to each page is a
        // write, so it is a COW fault while A maps the page.
        auto b = static_cast<unsigned char *>(mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));
        for (size_t i = 0; i < file_size; i += page) {
            b[i] = 0;
        }
        close(fd);
        memory_pressure();

        // Check that A still sees the file, and B the file except its writes.
        size_t bad_a = 0, bad_b = 0;
        for (size_t i = 0; i < file_size; i++) {
            bad_a += a[i] != expected(i);
            bad_b += b[i] != (i % page ? expected(i) : 0);
        }
        if (bad_a || bad_b) {
            printf("FAIL: round %d: %zu wrong bytes in the read-only mapping, %zu in the written one\n",
                   round, bad_a, bad_b);
            fails++;
        }
        munmap(b, file_size);
        munmap(a, file_size);
    }
    unlink(path);
    printf("SUMMARY: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
