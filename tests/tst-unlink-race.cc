/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Two threads repeatedly create, remove and close a file with the same name,
// and two other threads create and remove a directory with the same name.
// While one thread removes the file (or directory), the other may remove it
// too and create a new one with the same name. unlink() and rmdir() must
// remove the entry that the name refers to when they look it up; before this
// was fixed, ZFS was handed the vnode of an entry that had already been
// replaced (an assertion failure in bsd ZFS, a crash in the OpenZFS port).
// And open(O_CREAT) must not fail because another thread removed the file
// it had just created.
//
// Usage: tst-unlink-race.so [seconds] [dir]

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <errno.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 3;
    std::string dir = argc > 2 ? argv[2] : "/tmp";
    std::string file = dir + "/tst-unlink-race", subdir = dir + "/tst-rmdir-race";
    const char *path = file.c_str(), *dpath = subdir.c_str();
    std::atomic<bool> stop{false};
    std::atomic<long> cycles{0}, errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 2; t++) {
        threads.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                // The other thread may have created or removed it already.
                if (mkdir(dpath, 0755) != 0 && errno != EEXIST && errors++ < 5) {
                    printf("mkdir: %s\n", strerror(errno));
                }
                // rmdir() may also fail with EBUSY: OSv refuses to remove a
                // directory while anything else holds its vnode, and the
                // other thread may be releasing its last dentry for it.
                if (rmdir(dpath) != 0 && errno != ENOENT && errno != EBUSY &&
                        errors++ < 5) {
                    printf("rmdir: %s\n", strerror(errno));
                }
                cycles++;
            }
        });
        threads.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                int fd = open(path, O_CREAT | O_RDWR, 0644);
                if (fd < 0) {
                    if (errors++ < 5) {
                        printf("open: %s\n", strerror(errno));
                    }
                    continue;
                }
                // The other thread may have removed the file already.
                if (unlink(path) != 0 && errno != ENOENT && errors++ < 5) {
                    printf("unlink: %s\n", strerror(errno));
                }
                close(fd);
                cycles++;
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::seconds(secs));
    stop = true;
    for (auto& t : threads) {
        t.join();
    }
    unlink(path);
    rmdir(dpath);

    printf("%s: %ld create/remove cycles, %ld errors\n",
           errors ? "FAIL" : "PASS", cycles.load(), errors.load());
    return errors ? 1 : 0;
}
