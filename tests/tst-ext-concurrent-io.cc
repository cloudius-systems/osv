/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Concurrent reads and writes of different files on an ext filesystem must not
// see each other's data. libext reads and writes the partial first and last
// sector of a range through a buffer shared by the whole block device, and
// the VFS only locks each file, so without libext's own locking a read could
// return bytes of another file, and a write could store them on disk.
//
// ext-specific: it creates its files on the root filesystem, so build and run
// it with ext as the root filesystem:
//
//   scripts/build image=tests fs=ext
//   scripts/test.py --name tst-ext-concurrent-io.so

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <random>
#include <thread>
#include <unistd.h>
#include <vector>

static constexpr int nfiles = 2;
static constexpr size_t file_size = 1 << 20;
static constexpr int nthreads = 8;
static constexpr int ops_per_thread = 500;
static constexpr size_t max_len = 8192;

static const char *path(int f)
{
    return f == 0 ? "/tmp/tst-ext-concurrent-io-0" : "/tmp/tst-ext-concurrent-io-1";
}

// The byte at offset off of file f. The two files differ at every offset.
static unsigned char expected(int f, size_t off)
{
    return (unsigned char)((off * 131 + (off >> 9) * 7) ^ (f ? 0xa5 : 0x3c));
}

static bool write_whole_file(int f)
{
    std::vector<unsigned char> buf(file_size);
    for (size_t i = 0; i < file_size; i++) {
        buf[i] = expected(f, i);
    }
    int fd = open(path(f), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    bool ok = fd >= 0 && write(fd, buf.data(), file_size) == (ssize_t)file_size;
    if (fd >= 0) {
        close(fd);
    }
    return ok;
}

// Count the bytes of file f that differ from expected().
static size_t count_bad_bytes(int f)
{
    std::vector<unsigned char> buf(file_size);
    int fd = open(path(f), O_RDONLY);
    if (fd < 0 || read(fd, buf.data(), file_size) != (ssize_t)file_size) {
        if (fd >= 0) {
            close(fd);
        }
        return file_size;
    }
    close(fd);
    size_t bad = 0;
    for (size_t i = 0; i < file_size; i++) {
        bad += buf[i] != expected(f, i);
    }
    return bad;
}

// Run nthreads threads, alternating between the files, each doing
// ops_per_thread reads or writes of random length at random (mostly
// unaligned) offsets. Returns the number of failed operations.
static int run_threads(bool writes)
{
    std::atomic<int> failed{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < nthreads; t++) {
        threads.emplace_back([&, t] {
            int f = t % nfiles;
            int fd = open(path(f), writes ? O_RDWR : O_RDONLY);
            if (fd < 0) {
                failed++;
                return;
            }
            std::mt19937 rng(1000 + t);
            std::vector<unsigned char> buf(max_len);
            for (int i = 0; i < ops_per_thread; i++) {
                size_t off = rng() % file_size;
                size_t len = std::min<size_t>(1 + rng() % max_len, file_size - off);
                if (writes) {
                    for (size_t j = 0; j < len; j++) {
                        buf[j] = expected(f, off + j);
                    }
                    if (pwrite(fd, buf.data(), len, off) != (ssize_t)len) {
                        failed++;
                    }
                } else {
                    bool ok = pread(fd, buf.data(), len, off) == (ssize_t)len;
                    for (size_t j = 0; ok && j < len; j++) {
                        ok = buf[j] == expected(f, off + j);
                    }
                    failed += !ok;
                }
            }
            close(fd);
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    return failed;
}

int main()
{
    int fails = 0;
    for (int f = 0; f < nfiles; f++) {
        if (!write_whole_file(f)) {
            printf("FAIL: could not create %s\n", path(f));
            return 1;
        }
    }

    // Check that concurrent reads of different files return their own data.
    int bad_reads = run_threads(false);
    printf("%s: %d of %d concurrent reads returned wrong data\n",
           bad_reads ? "FAIL" : "PASS", bad_reads, nthreads * ops_per_thread);
    fails += bad_reads != 0;

    // Check that concurrent writes of different files store their own data.
    // Every write stores the bytes the file is expected to have anyway, so
    // any difference afterwards came from another file.
    int bad_writes = run_threads(true);
    for (int f = 0; f < nfiles; f++) {
        size_t bad = count_bad_bytes(f);
        printf("%s: %zu bytes of %s wrong after concurrent writes%s\n",
               bad ? "FAIL" : "PASS", bad, path(f), bad_writes ? " (some writes failed)" : "");
        fails += bad != 0 || bad_writes != 0;
    }

    for (int f = 0; f < nfiles; f++) {
        unlink(path(f));
    }
    printf("SUMMARY: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
