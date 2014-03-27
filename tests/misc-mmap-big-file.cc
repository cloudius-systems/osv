/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <functional>
#include <thread>

#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>


#define PAGE_SIZE (4 << 10)
#define FNAME "/tmp/mmap-file-test"
#define FNAMEANON "/tmp/mmap-file-anon"
#define FNAMESMALL "/tmp/mmap-file-test-small"
#define FNAMESMALL2 "/tmp/mmap-file-test-small2"
#define FNAMESMALL3 "/tmp/mmap-file-test-small3"

char buf[PAGE_SIZE];

typedef std::chrono::time_point<std::chrono::high_resolution_clock> tpoint;
void report(const char *phase, unsigned long passes,
            unsigned long pages, const tpoint start, const tpoint end)
{
    auto usec = std::chrono::duration_cast<std::chrono::microseconds> (end - start).count();
    std::cout << phase << " OK (" << float(usec) / (pages * passes) << " usec / page)\n";
}

char ch(int i)
{
    return '@' + (i % ('}' - '@'));
}

void map_pass(unsigned char *addr, unsigned long passes, unsigned long pages,
              std::function<unsigned long (unsigned long)> conv)
{
    for (unsigned long j = 0; j < pages * passes; ++j) {
        auto i = conv(j);
        char x = *(addr + i * PAGE_SIZE);
        assert(x == ch(i));
    }
}

void map_pass(const char *phase, unsigned char *addr, unsigned long passes,
              unsigned long pages, std::function<unsigned long (unsigned long)> conv)
{
    auto start = std::chrono::high_resolution_clock::now();
    map_pass(addr, passes, pages, conv);
    auto end = std::chrono::high_resolution_clock::now();
    report(phase, passes, pages, start, end);
}

char safe_buffers[PAGE_SIZE][2];
char test_string[] = "test_string";

int main(int ac, char** av)
{
    int fd = open(FNAME, O_RDWR | O_CREAT, 0666);
    assert(fd >= 0);
    int fdanon = open(FNAMEANON, O_RDWR | O_CREAT, 0666);
    assert(fdanon >= 0);
    int fdsmall = open(FNAMESMALL, O_RDWR | O_CREAT, 0666);
    assert(fdsmall >= 0);

    int fdsmall2 = open(FNAMESMALL2, O_RDWR | O_CREAT, 0666);
    assert(fdsmall2 >= 0);

    int fdsmall3 = open(FNAMESMALL3, O_RDWR | O_CREAT, 0666);
    assert(fdsmall3 >= 0);

    assert(write(fdsmall, test_string, sizeof(test_string)) == sizeof(test_string));

    lseek(fdsmall2, PAGE_SIZE, SEEK_SET);
    assert(write(fdsmall2, test_string, sizeof(test_string)) == sizeof(test_string));

    lseek(fdsmall3, 128 << 10, SEEK_SET);
    assert(write(fdsmall3, test_string, sizeof(test_string)) == sizeof(test_string));

    struct sysinfo sinfo;
    assert(sysinfo(&sinfo) == 0);

    std::cout << "Total Ram " << (sinfo.totalram >> 20) << " Mb\n";
    unsigned long pages = sinfo.totalram / PAGE_SIZE;

    for (unsigned long i = 0; i < pages * 2; ++i) {
        memset(buf, ch(i), PAGE_SIZE);
        assert(write(fd, buf, PAGE_SIZE) == PAGE_SIZE);
    }

    // 256k should take at least 2 ZFS buffers.
    for (unsigned long i = 0; i < (256 << 10) / PAGE_SIZE; ++i) {
        memset(buf, '-', PAGE_SIZE);
        assert(write(fdanon, buf, PAGE_SIZE) == PAGE_SIZE);
    }

    close(fd);
    close(fdanon);
    close(fdsmall);
    close(fdsmall2);
    close(fdsmall3);
    std::cout << "Write done\n";

    fdsmall2 = open(FNAMESMALL2, O_RDONLY);
    assert(fd >= 0);
    void *retsmall2 = mmap(nullptr, 2 * PAGE_SIZE, PROT_READ, MAP_SHARED, fdsmall2, PAGE_SIZE);
    assert(memcmp(test_string, retsmall2, sizeof(test_string)) == 0);

    fdsmall3 = open(FNAMESMALL3, O_RDONLY);
    assert(fd >= 0);
    void *retsmall3 = mmap(nullptr, (128 << 10) + PAGE_SIZE, PROT_READ, MAP_SHARED, fdsmall3, 128 << 10);
    assert(memcmp(test_string, retsmall3, sizeof(test_string)) == 0);

    fdsmall = open(FNAMESMALL, O_RDONLY);
    assert(fd >= 0);
    void *retsmall = mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_SHARED, fdsmall, 0);
    assert(memcmp(test_string, retsmall, sizeof(test_string)) == 0);

    munmap(retsmall, PAGE_SIZE);
    munmap(retsmall2, PAGE_SIZE);
    munmap(retsmall3, PAGE_SIZE);
    close(fdsmall);
    close(fdsmall2);
    close(fdsmall3);
    std::cout << "Small files ended OK\n";

    // We need to be careful with mappings in the edges of the file mapping. If we are not careful, invalidation will also
    // destroy adjacent mappings. It is hard for us to destroy a specific mapping, so what we are going to do is to map a
    // different file (to avoid recency issues), in the beginning of the test. By the end of it, the new file should have
    // flushed everything from this file out of the cache.
    fdanon = open(FNAMEANON, O_RDONLY);
    assert(fdanon >= 0);
    void *retanon = mmap(nullptr, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    void *retself = mmap(retanon + 2 * PAGE_SIZE, 2 * PAGE_SIZE, PROT_READ, MAP_SHARED | MAP_FIXED, fdanon, 2 * PAGE_SIZE);
    void *retanon2 = mmap(retself + 2 * PAGE_SIZE, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0, 0);

    for (int i = 0; i < 2; i++) {
        memcpy(retanon + i * PAGE_SIZE, retself + i * PAGE_SIZE, PAGE_SIZE);
        memcpy(retanon2 + i * PAGE_SIZE, retself + i * PAGE_SIZE, PAGE_SIZE);
        memcpy(safe_buffers[i], retself + i * PAGE_SIZE, PAGE_SIZE);
    }

    fd = open(FNAME, O_RDONLY);
    assert(fd >= 0);

    std::default_random_engine generator;
    std::uniform_int_distribution<unsigned long> distribution(0, (pages * 2) - 1);


    void *ret = mmap(nullptr, sinfo.totalram * 2, PROT_READ, MAP_SHARED, fd, 0);
    unsigned char *base = static_cast<unsigned char *>(ret);

    // Read the whole file twice. The file itself is twice the size of memory,
    // so once evictions will surely happen. We need to see how well we handle
    // them.
    map_pass("Double Pass", base, 4, pages, [&](unsigned long j) { return j % (pages * 2); });

    // In this pass, we will keep looping in a subset of half of memory. The number of evictions
    // is expected to be way lower in this case.
    map_pass("Recency", base, 4, pages, [&](unsigned long j) { return j % (pages / 2); });

    // Now with random access, we should pretty much destroy any pattern
    map_pass("Random Access", base, 1, pages, [&](unsigned long j) { return distribution(generator); });

    std::thread t1( [&] { map_pass(base, 4, pages, [&](unsigned long j) { return j % (pages * 2); } ); } );
    std::thread t2( [&] { map_pass(base, 4, pages, [&](unsigned long j) { return j % (pages / 2); } ); } );
    std::thread t3( [&] { map_pass(base, 4, pages, [&](unsigned long j) { return j % (pages * 2); } ); } );
    std::thread t4( [&] { map_pass(base, 4, pages, [&](unsigned long j) { return j % (pages / 2); } ); } );

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    std::cout << "Threaded pass 1 address ended OK\n";

    ret = mmap(nullptr, sinfo.totalram * 2, PROT_READ, MAP_SHARED, fd, 0);
    unsigned char *base1 = static_cast<unsigned char *>(ret);

    ret = mmap(nullptr, sinfo.totalram * 2, PROT_READ, MAP_SHARED, fd, 0);
    unsigned char *base2 = static_cast<unsigned char *>(ret);

    ret = mmap(nullptr, sinfo.totalram * 2, PROT_READ, MAP_SHARED, fd, 0);
    unsigned char *base3 = static_cast<unsigned char *>(ret);

    std::thread ta1( [&] { map_pass(base,  4, pages, [&](unsigned long j) { return j % (pages * 2); } ); } );
    std::thread ta2( [&] { map_pass(base1, 4, pages, [&](unsigned long j) { return j % (pages / 2); } ); } );
    std::thread ta3( [&] { map_pass(base2, 4, pages, [&](unsigned long j) { return j % (pages * 2); } ); } );
    std::thread ta4( [&] { map_pass(base3, 4, pages, [&](unsigned long j) { return j % (pages / 2); } ); } );

    ta1.join();
    ta2.join();
    ta3.join();
    ta4.join();
    std::cout << "Threaded pass many addresses ended OK\n";

    for (int i = 0; i < 2; i++) {
        assert(memcmp(retanon + i * PAGE_SIZE, safe_buffers[i], PAGE_SIZE) == 0);
        assert(memcmp(retanon2 + i * PAGE_SIZE, safe_buffers[i], PAGE_SIZE) == 0);
    }

    std::cout << "Anonymous boundaries ended OK\n";

    close(fd);
    close(fdanon);

    munmap(base1, sinfo.totalram * 2);
    munmap(base2, sinfo.totalram * 2);
    munmap(base3, sinfo.totalram * 2);

    // Make sure the split code works.
    munmap(base + sinfo.totalram, PAGE_SIZE);
    map_pass(base, 1, pages, [&](unsigned long j) { return j; }); 
    munmap(base, sinfo.totalram * 2);
    std::cout << "Split mapping ended OK\n";

    std::cout << "PASSED\n";
    return 0;
}
