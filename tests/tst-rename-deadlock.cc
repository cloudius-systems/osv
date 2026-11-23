/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Renaming a file while other threads open and close another hard link to it
// must not deadlock. This used to hang within seconds, in two ways:
//  - sys_rename() locked the file before its directory, while lookup locks
//    the directory and then (in vget()) the file, and vget() waited for the
//    file's lock while holding the global vnode table lock;
//  - drele() of the last reference to a dentry took dentry_hash_lock and then
//    the vnode lock, the reverse of namei()'s order.
//
// Needs a filesystem with hard links, such as the default ZFS root. On a
// deadlock the test reports no progress and aborts, which also stops the
// guest even though the deadlocked threads are still there; a watchdog
// aborts it too if it hangs after its last progress check.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <vector>

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 5;
    const char *a = "/tmp/tst-rename-deadlock-a";
    const char *b = "/tmp/tst-rename-deadlock-b";
    const char *c = "/tmp/tst-rename-deadlock-c";
    unlink(a); unlink(b); unlink(c);
    int fd = open(a, O_CREAT | O_RDWR, 0644);
    if (fd < 0 || write(fd, "x", 1) != 1) {
        printf("FAIL: could not create %s\n", a);
        return 1;
    }
    close(fd);
    if (link(a, b) != 0) {
        printf("SKIP: no hard links on this filesystem\n");
        unlink(a);
        return 0;
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

    std::atomic<bool> stop{false};
    std::atomic<long> ops{0}, errors{0};
    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        while (!stop) {
            if (rename(a, c) || rename(c, a)) {
                errors++;
            }
            ops++;
        }
    });
    for (int i = 0; i < 3; i++) {
        threads.emplace_back([&] {
            while (!stop) {
                int f = open(b, O_RDONLY);
                if (f < 0) {
                    errors++;
                    continue;
                }
                close(f);
                ops++;
            }
        });
    }
    long last = -1;
    for (int s = 0; s < seconds; s++) {
        sleep(1);
        long now = ops;
        if (now == last) {
            printf("FAIL: no progress for a second after %ld operations: deadlock\n", now);
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
    unlink(a); unlink(b);
    printf("%s: %ld operations in %d seconds, %ld errors\n", errors ? "FAIL" : "PASS", ops.load(), seconds, errors.load());
    return errors ? 1 : 0;
}
