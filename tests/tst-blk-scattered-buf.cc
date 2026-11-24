/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Raw block-device I/O from a buffer that is contiguous only in virtual
// memory.  The buffer is built from single 4K mappings, and after each one a
// separate "gap" page is mapped and touched, so no two neighbouring buffer
// pages are physically adjacent.  A driver that maps such a buffer for DMA by
// adding offsets to the physical address of its first byte transfers the
// wrong pages: the data read back differs, and the gap pages (which no I/O
// names) get overwritten.
//
// Usage: tst-blk-scattered-buf.so <device> (default /dev/vblk1).  The test
// overwrites the first 2 MiB of the device: give it a scratch disk.

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>

static const size_t page = 4096;
static const size_t npages = 258;   // 1 MiB + a misaligned start + a spare page

// npages virtually contiguous pages, each its own mapping, interleaved in
// physical memory with the pages in gaps[].
static uint8_t *scattered(void **gaps)
{
    auto *base = static_cast<uint8_t*>(mmap(nullptr, npages * page, PROT_NONE,
                                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (base == MAP_FAILED) {
        return nullptr;
    }
    for (size_t i = 0; i < npages; i++) {
        void *p = mmap(base + i * page, page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        gaps[i] = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED || gaps[i] == MAP_FAILED) {
            return nullptr;
        }
        base[i * page] = 0;
        memset(gaps[i], 0x5a, page);
    }
    return base;
}

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/vblk1";
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("SKIP: cannot open %s\n", dev);
        return 0;
    }
    static void *wgaps[npages], *rgaps[npages];
    uint8_t *w = scattered(wgaps), *r = scattered(rgaps);
    if (!w || !r) {
        printf("FAIL: mmap\n");
        return 1;
    }
    // One page, two pages, and PRP-list sized transfers, page aligned and not.
    const size_t sizes[] = {4096, 8192, 12288, 65536, 1 << 20};
    const size_t offsets[] = {0, 512};
    int failures = 0, cases = 0;
    for (size_t s : sizes) {
        for (size_t o : offsets) {
            cases++;
            for (size_t i = 0; i < s; i++) {
                w[o + i] = static_cast<uint8_t>((i * 131 + s + o) ^ (i >> 12));
            }
            memset(r + o, 0xa5, s);
            if (pwrite(fd, w + o, s, 0) != (ssize_t)s || fsync(fd) != 0 ||
                pread(fd, r + o, s, 0) != (ssize_t)s) {
                printf("FAIL: I/O error size=%zu misalign=%zu\n", s, o);
                failures++;
                continue;
            }
            if (memcmp(w + o, r + o, s) != 0) {
                size_t i = 0;
                while (w[o + i] == r[o + i]) {
                    i++;
                }
                printf("FAIL: data mismatch size=%zu misalign=%zu at byte %zu\n", s, o, i);
                failures++;
            }
        }
    }
    cases++;
    int clobbered = 0;
    for (size_t i = 0; i < npages; i++) {
        for (void *g : {wgaps[i], rgaps[i]}) {
            auto *b = static_cast<uint8_t*>(g);
            for (size_t j = 0; j < page; j++) {
                if (b[j] != 0x5a) {
                    clobbered++;
                    break;
                }
            }
        }
    }
    if (clobbered) {
        printf("FAIL: %d unrelated pages were overwritten by the I/O\n", clobbered);
        failures++;
    }
    close(fd);
    printf("SUMMARY: %d tests, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
