/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Check that writes to a MAP_PRIVATE file mapping are not lost when several
// threads write to the same page at the same time, so that each takes a
// copy-on-write fault on the same page. Only one private copy may end up
// mapped, and it must hold every thread's write; a second copy made from the
// file and mapped over the first would lose the writes already made to it.
//
// Usage: tst-mmap-private-race.so [pages] [path]

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static constexpr int nthreads = 4;
static constexpr size_t page_size = 4096;
static constexpr unsigned char fill = 0xa5;

int main(int argc, char **argv)
{
    size_t pages = argc > 1 ? atol(argv[1]) : 4096;
    const char *path = argc > 2 ? argv[2] : "/tmp/tst-mmap-private-race";
    size_t size = pages * page_size;

    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    std::vector<unsigned char> buf(size, fill);
    if (write(fd, buf.data(), size) != (ssize_t)size) {
        perror("write");
        return 1;
    }
    auto *p = static_cast<unsigned char *>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    // The threads meet at a barrier before each page, then each writes its
    // own slot of that page, so that their first accesses to the page are
    // writes made at about the same time.
    std::atomic<size_t> arrived{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < nthreads; t++) {
        threads.emplace_back([&, t] {
            for (size_t i = 0; i < pages; i++) {
                arrived.fetch_add(1);
                while (arrived.load() < (i + 1) * nthreads) {
                }
                p[i * page_size + t * 64] = t + 1;
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    // Check that every thread's write is still there.
    size_t lost = 0;
    for (size_t i = 0; i < pages; i++) {
        for (int t = 0; t < nthreads; t++) {
            if (p[i * page_size + t * 64] != t + 1 && lost++ < 5) {
                printf("page %zu: thread %d's write lost\n", i, t);
            }
        }
    }
    munmap(p, size);
    close(fd);
    unlink(path);
    printf("%s: %zu pages, %zu writes lost\n", lost ? "FAIL" : "PASS", pages, lost);
    return lost ? 1 : 0;
}
