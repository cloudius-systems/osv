/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Correctness stress, not a benchmark. Run on an ext4 mount:
// tests/tst-ext4-lifecycle.so /data
// Deterministic delayed-worker coverage is in tst-ext4-workers.py.
#include <atomic>
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <thread>
#include <sys/statfs.h>
#include <unistd.h>
#include <vector>

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "/data";
    struct statfs fs;
    if (statfs(dir.c_str(), &fs) != 0) {
        perror("statfs");
        return 1;
    }
    printf("MOUNT_PROOF path=%s f_type=0x%lx\n", dir.c_str(), (unsigned long)fs.f_type);
    if (fs.f_type != 0xef53) {
        fprintf(stderr, "FAIL: lifecycle test requires ext4, not a fallback filesystem\n");
        return 1;
    }
    std::string path = dir + "/tst-ext4-lifecycle.dat";
    std::string reuse = dir + "/tst-ext4-lifecycle-reuse.dat";
    const size_t window = 256 * 1024;
    std::vector<char> data(8 * window);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = char(17 + i / window);
    }
    for (bool remove : {false, true}) {
        for (unsigned round = 0; round < 20; ++round) {
            int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
            assert(fd >= 0);
            assert(write(fd, data.data(), data.size()) == ssize_t(data.size()));
            assert(fsync(fd) == 0);
            std::atomic<bool> started{false}, mutated{false};
            std::thread reader([&] {
                std::vector<char> buf(4096);
                size_t off = 0;
                do {
                    ssize_t n = pread(fd, buf.data(), buf.size(), off);
                    assert(n >= 0 && n <= ssize_t(buf.size()));
                    for (ssize_t i = 0; i < n; ++i) {
                        assert(buf[i] == data[off + i]);
                    }
                    // Existing libext truncates open-unlinked files early;
                    // accept EOF here, but never another file's reused blocks.
                    // This test does NOT certify POSIX open-unlink semantics.
                    started = true;
                    off = (off + buf.size()) % data.size();
                } while (!mutated.load());
            });
            while (!started.load()) std::this_thread::yield();
            if (remove) {
                assert(unlink(path.c_str()) == 0);
            } else {
                assert(ftruncate(fd, 0) == 0);
            }
            // Encourage block reuse while the old reader/worker pool is live.
            int other = open(reuse.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
            assert(other >= 0);
            std::vector<char> poison(data.size(), char(0xa5));
            assert(write(other, poison.data(), poison.size()) == ssize_t(poison.size()));
            assert(fsync(other) == 0);
            mutated = true;
            reader.join();
            if (!remove) {
                char c;
                assert(pread(fd, &c, 1, 0) == 0);
                assert(unlink(path.c_str()) == 0);
            }
            assert(close(fd) == 0);
            assert(close(other) == 0);
            assert(unlink(reuse.c_str()) == 0);
        }
        printf("PASS concurrent read/%s and block reuse\n", remove ? "unlink" : "truncate");
    }
    puts("PASS ext4 lifecycle complete");
    return 0;
}
