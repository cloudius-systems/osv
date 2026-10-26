/*
 * Copyright (C) 2026 Greg Burd
 * BSD license as described in the top-level LICENSE file.
 */
// Exercise actual benchmark I/O and verification without running its benchmark
// main or publishing rates. The swapped-window control must be rejected.
#include <cassert>
#include <algorithm>
static bool fail_sync;
#include <unistd.h>
#include <errno.h>
static int oracle_fsync(int fd)
{
    if (fail_sync) { errno = EIO; return -1; }
    return fsync(fd);
}
#define fsync oracle_fsync
#define main ext4_bench_main
#include "tst-ext4-bench.cc"
#undef main
#undef fsync

int main(int argc, char** argv)
{
    std::string path = std::string(argc > 1 ? argv[1] : "/tmp") + "/ext4-oracle-XXXXXX";
    std::vector<char> name(path.begin(), path.end());
    name.push_back(0);
    int fd = mkstemp(name.data());
    assert(fd >= 0);
    const size_t window = 256 * 1024;
    const size_t total = 2 * window + 37;
    assert(bench_write(name.data(), total, 4093, true) > 0);
    assert(bench_read_seq(name.data(), total, 8191, true) > 0);
    std::vector<char> first(window), second(window);
    assert(pread(fd, first.data(), window, 0) == ssize_t(window));
    assert(pread(fd, second.data(), window, window) == ssize_t(window));
    assert(pwrite(fd, second.data(), window, 0) == ssize_t(window));
    assert(pwrite(fd, first.data(), window, window) == ssize_t(window));
    assert(bench_read_seq(name.data(), total, 4096, true) < 0);
    puts("PASS swapped 256KiB windows rejected");
    assert(bench_write(name.data(), total, 4093, true) > 0);
    struct stat st;
    assert(fstat(fd, &st) == 0 && st.st_size == off_t(total));
    assert(ftruncate(fd, window) == 0);
    assert(bench_read_seq(name.data(), total, 4096, true) < 0);
    assert(bench_read_rand(name.data(), 2 * window, window, 16) < 0);
    fail_sync = true;
    assert(bench_write(name.data(), 4096, 4096, true) < 0);
    assert(close(fd) == 0);
    assert(unlink(name.data()) == 0);
    puts("PASS exact byte counts and fsync error propagation");
}
