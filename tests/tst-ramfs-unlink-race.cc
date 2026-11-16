/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// ramfs node lifetime under concurrent create, stat, open and unlink.
//
// A ramfs node must outlive every vnode that points at it, not only every
// open file. Threads here create, stat, open, write, read and unlink a few
// names in a private ramfs mount, so a lookup or a stat often holds a vnode
// for a node another thread is unlinking. Every thread writes the same
// byte, so any stat must describe a regular file of at most one byte, and
// any read through a descriptor that just wrote must return that byte.
// An unlink that loses a race to another unlink of the same name must say
// ENOENT. A freed node shows up as a wrong mode, size or byte, or a crash.
//
// Usage: tst-ramfs-unlink-race [seconds]   (default 3, so CI runs it; the
//        red case crashes within about a second)

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static std::atomic<bool> stop{false};
static std::atomic<long> bad{0}, creates{0}, stats{0}, unlinks{0};

static void report(const char *what, const std::string &path, int err)
{
    if (bad.fetch_add(1) < 20) {
        printf("FAIL: %s %s: %s\n", what, path.c_str(), strerror(err));
    }
}

static void worker(const std::string &dir, unsigned seed)
{
    const char byte = 'x';
    while (!stop.load(std::memory_order_relaxed)) {
        std::string path = dir + "/f" + std::to_string(rand_r(&seed) % 4);
        switch (rand_r(&seed) % 3) {
        case 0: {
            int fd = open(path.c_str(), O_CREAT | O_RDWR, 0644);
            if (fd < 0) {
                if (errno != ENOENT && errno != EEXIST) report("open", path, errno);
                break;
            }
            creates++;
            char got = 0;
            if (pwrite(fd, &byte, 1, 0) != 1) {
                report("pwrite", path, errno);
            } else {
                ssize_t n = pread(fd, &got, 1, 0);
                if (n != 1) {
                    report("pread", path, errno);
                } else if (got != byte) {
                    report("pread returned a different byte", path, 0);
                }
            }
            struct stat st;
            if (fstat(fd, &st) != 0) {
                report("fstat", path, errno);
            } else if (!S_ISREG(st.st_mode)) {
                report("fstat (not a regular file)", path, EINVAL);
            }
            close(fd);
            break;
        }
        case 1: {
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                stats++;
                if (!S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > 1) {
                    report("stat (mode or size is wrong)", path, EINVAL);
                }
            } else if (errno != ENOENT) {
                report("stat", path, errno);
            }
            break;
        }
        default:
            if (unlink(path.c_str()) == 0) {
                unlinks++;
            } else if (errno != ENOENT) {
                report("unlink", path, errno);
            }
        }
    }
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 3;
    const char *dir = "/tmp/ramfs-unlink-race";

    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        printf("FAIL: mkdir %s: %s\n", dir, strerror(errno));
        return 1;
    }
    if (mount("none", dir, "ramfs", 0, nullptr) != 0) {
        printf("FAIL: mount ramfs on %s: %s\n", dir, strerror(errno));
        return 1;
    }

    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 4; i++) {
        threads.emplace_back(worker, std::string(dir), i + 1);
    }
    struct timespec ts = {seconds, 0};
    nanosleep(&ts, nullptr);
    stop = true;
    for (auto &t : threads) {
        t.join();
    }

    for (int i = 0; i < 4; i++) {
        unlink((std::string(dir) + "/f" + std::to_string(i)).c_str());
    }
    if (umount(dir) != 0) {
        report("umount", dir, errno);
    }
    rmdir(dir);

    printf("%ld creates, %ld stats, %ld unlinks in %d s\n",
           creates.load(), stats.load(), unlinks.load(), seconds);
    if (creates == 0 || unlinks == 0) {
        report("no work was done", dir, EINVAL);
    }
    if (bad) {
        printf("SUMMARY: %ld problem(s)\n", bad.load());
        return 1;
    }
    printf("PASS: ramfs node lifetime held under concurrent create, stat and unlink\n");
    return 0;
}
