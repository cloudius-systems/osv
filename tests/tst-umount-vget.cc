/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Unmounting one filesystem while other threads create vnodes on another
// must not deadlock. vget() marks the mount busy (vfs_busy()) while it holds
// the vnode table lock, and sys_umount2() holds mount_lock while the
// unmount releases the mount's vnodes (vrele()), which takes the vnode table
// lock. When vfs_busy() took mount_lock, the two orders met and the threads
// stopped within a few seconds.
//
// One thread mounts a ramfs, creates and removes a file in it (so that the
// unmount has a vnode to release) and unmounts it, in a loop. Three threads
// create, open and remove files in the parent directory, so that vget()
// keeps creating vnodes there. On a deadlock the test reports no progress
// and aborts, which also stops the guest even though the deadlocked threads
// are still there; a watchdog aborts it too if it hangs after its last
// progress check.
//
// Usage: tst-umount-vget [seconds] [dir]   (default 5 s in /tmp)

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
#include <unistd.h>

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 5;
    std::string dir = argc > 2 ? argv[2] : "/tmp";
    std::string mnt = dir + "/tst-umount-vget-mnt";
    if (mkdir(mnt.c_str(), 0755) != 0 && errno != EEXIST) {
        printf("FAIL: mkdir %s: %s\n", mnt.c_str(), strerror(errno));
        return 1;
    }

    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        for (int i = 0; i < (seconds + 10) * 10 && !done; i++) {
            usleep(100000);
        }
        if (!done) {
            printf("FAIL: still running 10 seconds after it should have ended: deadlock\n");
            fflush(stdout);
            abort();
        }
    });

    // Every thread counts its iterations in ops; the unmount thread also
    // counts its cycles and the mount calls that failed.
    std::atomic<bool> stop{false};
    std::atomic<long> ops{0}, cycles{0}, errors{0};
    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        std::string f = mnt + "/f";
        while (!stop) {
            if (mount("none", mnt.c_str(), "ramfs", 0, nullptr) != 0) {
                printf("FAIL: mount ramfs on %s: %s\n", mnt.c_str(), strerror(errno));
                errors++;
                break;
            }
            int fd = open(f.c_str(), O_CREAT | O_RDWR, 0644);
            if (fd < 0 || close(fd) != 0 || unlink(f.c_str()) != 0) {
                errors++;
            }
            if (umount(mnt.c_str()) != 0) {
                printf("FAIL: umount %s: %s\n", mnt.c_str(), strerror(errno));
                errors++;
                break;
            }
            cycles++;
            ops++;
        }
    });
    for (int t = 0; t < 3; t++) {
        threads.emplace_back([&, t] {
            std::string f = dir + "/tst-umount-vget-" + std::to_string(t);
            while (!stop) {
                int fd = open(f.c_str(), O_CREAT | O_RDWR, 0644);
                if (fd < 0 || close(fd) != 0 || unlink(f.c_str()) != 0) {
                    errors++;
                }
                ops++;
            }
        });
    }

    long last = -1;
    for (int s = 0; s < seconds; s++) {
        sleep(1);
        long now = ops;
        if (now == last) {
            printf("FAIL: no progress for a second after %ld unmounts, %ld operations: deadlock\n",
                   cycles.load(), now);
            fflush(stdout);
            abort();
        }
        last = now;
    }
    stop = true;
    for (auto &t : threads) {
        t.join();
    }
    done = true;
    watchdog.join();
    rmdir(mnt.c_str());

    bool ok = errors == 0 && cycles > 0;
    printf("%s: %ld unmounts, %ld operations in %d seconds, %ld errors\n",
           ok ? "PASS" : "FAIL", cycles.load(), ops.load(), seconds, errors.load());
    return ok ? 0 : 1;
}
