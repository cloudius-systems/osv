/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Fill a ZFS pool until only leave-MB is free, run fsync threads for a while,
// fill the rest to ENOSPC, keep fsyncing on the full pool, then remove the
// fill file and check that the space comes back.
//
// Before the ZIL space check in zio_alloc_zil(), intent-log blocks were not
// counted by any space check, so under fsync load they took the slop that
// spa_sync() needs: spa_sync() then got ENOSPC, the pool suspended and every
// thread blocked forever.  A watchdog turns "no progress for 60 s" into a
// FAIL and aborts, so a hang shows up as a failure rather than a timeout.
//
// Usage: tst-zfs-zil-full.so [fsync-threads=4] [seconds=20] [leave-MB=128] [dir=/tmp]
// With 0 fsync threads it only fills and frees the pool (plain ENOSPC path).

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <string>
#include <sys/statvfs.h>
#include <thread>
#include <unistd.h>
#include <vector>

using clk = std::chrono::steady_clock;

static int tests, failures;
static std::atomic<unsigned long> progress{0};
static std::atomic<bool> stop{false}, done{false};

static void report(bool ok, const char *what)
{
    tests++;
    if (!ok) {
        failures++;
    }
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
}

static unsigned long long free_bytes(const std::string &dir)
{
    struct statvfs st;
    if (statvfs(dir.c_str(), &st) != 0) {
        return 0;
    }
    return (unsigned long long)st.f_bavail * st.f_frsize;
}

static long long refused()
{
    // Exported by the BSD ZFS libsolaris.so that has the check.
    auto *p = (unsigned long long *)dlsym(RTLD_DEFAULT, "zfs_zil_space_refused");
    return p ? (long long)*p : -1;
}

int main(int argc, char **argv)
{
    int nthr = argc > 1 ? atoi(argv[1]) : 4;
    int secs = argc > 2 ? atoi(argv[2]) : 20;
    unsigned long long leave = (argc > 3 ? atoll(argv[3]) : 128) << 20;
    std::string dir = argc > 4 ? argv[4] : "/tmp";
    std::string fill = dir + "/zil-full-fill";

    std::thread watchdog([] {
        unsigned long last = progress.load();
        auto t = clk::now();
        while (!done.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            unsigned long now = progress.load();
            if (now != last) {
                last = now;
                t = clk::now();
            } else if (clk::now() - t > std::chrono::seconds(60)) {
                printf("FAIL: no progress for 60 s (pool suspended?), refused=%lld\n",
                       refused());
                fflush(stdout);
                abort();
            }
        }
    });

    long long refused0 = refused();
    unsigned long long free0 = free_bytes(dir);
    printf("fsync threads %d, %d s, leave %llu MB, %llu MB free, refused=%lld\n",
           nthr, secs, leave >> 20, free0 >> 20, refused0);

    // Each fsync thread gets its file before the pool fills.
    std::vector<int> fds;
    char buf[4096];
    for (int t = 0; t < nthr; t++) {
        std::string p = dir + "/zil-full-" + std::to_string(t);
        int fd = open(p.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
        memset(buf, 'a' + t, sizeof(buf));
        bool ok = fd >= 0;
        for (int i = 0; ok && i < 256; i++) {
            ok = pwrite(fd, buf, sizeof(buf), (off_t)i * 4096) == sizeof(buf);
        }
        report(ok && fsync(fd) == 0, "create an fsync file");
        fds.push_back(fd);
    }

    // Phase 1: fill the pool until only `leave` bytes are free.
    int ffd = open(fill.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    report(ffd >= 0, "create the fill file");
    std::vector<char> chunk(1 << 20, 'z');
    unsigned long long filled = 0;
    int fill_errno = 0;
    while (ffd >= 0 && free_bytes(dir) > leave) {
        ssize_t n = write(ffd, chunk.data(), chunk.size());
        if (n < 0) {
            fill_errno = errno;
            break;
        }
        filled += n;
        progress++;
    }
    if (ffd >= 0) {
        fsync(ffd);
    }
    printf("filled %llu MB, %llu MB free, refused=%lld\n", filled >> 20,
           free_bytes(dir) >> 20, refused());

    // Phase 2: overwrite + fdatasync with `leave` free.
    std::atomic<unsigned long> fsyncs{0}, fsync_enospc{0}, fsync_err{0};
    std::vector<std::thread> th;
    for (int t = 0; t < nthr; t++) {
        th.emplace_back([&, t] {
            char b[4096];
            memset(b, 'A' + t, sizeof(b));
            for (unsigned i = 0; !stop.load(); i++) {
                if (pwrite(fds[t], b, sizeof(b), (off_t)(i % 256) * 4096) != sizeof(b)) {
                    if (errno != ENOSPC && errno != EDQUOT) {
                        fsync_err++;
                        break;
                    }
                    fsync_enospc++;
                } else if (fdatasync(fds[t]) != 0) {
                    if (errno != ENOSPC) {
                        fsync_err++;
                        break;
                    }
                    fsync_enospc++;
                } else {
                    fsyncs++;
                }
                progress++;
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::seconds(secs));
    printf("with %llu MB left: %lu fsyncs, refused=%lld\n", leave >> 20, fsyncs.load(),
           refused());

    // Phase 3: fill the rest while the fsync threads run.  Near-full writers
    // can be throttled indefinitely, so the fill runs in its own thread and
    // the fsync threads stop after `secs`, letting it finish.
    std::thread filler([&] {
        while (ffd >= 0 && fill_errno == 0) {
            ssize_t n = write(ffd, chunk.data(), chunk.size());
            if (n < 0) {
                fill_errno = errno;
                break;
            }
            filled += n;
            progress++;
        }
    });
    std::this_thread::sleep_for(std::chrono::seconds(secs));
    stop = true;
    for (auto &x : th) {
        x.join();
    }
    filler.join();
    printf("fsync threads: %lu fsyncs, %lu ENOSPC, %lu other errors, refused=%lld\n",
           fsyncs.load(), fsync_enospc.load(), fsync_err.load(), refused());
    report(fsync_err.load() == 0, "fsync threads saw no error other than ENOSPC");
    printf("filled %llu MB, then errno %d (%s), refused=%lld\n", filled >> 20,
           fill_errno, strerror(fill_errno), refused());
    report(fill_errno == ENOSPC || fill_errno == EDQUOT,
           "filling the pool ends in ENOSPC");

    // Phase 4: fsync on the full pool must return (success or ENOSPC).
    int ok4 = 0;
    for (int t = 0; t < nthr; t++) {
        char b[4096];
        memset(b, 'q', sizeof(b));
        if (pwrite(fds[t], b, sizeof(b), 0) == sizeof(b)) {
            ok4 += fdatasync(fds[t]) == 0 || errno == ENOSPC;
        } else {
            ok4 += errno == ENOSPC || errno == EDQUOT;
        }
        progress++;
    }
    report(ok4 == nthr, "write + fdatasync on the full pool return");
    for (int t = 0; t < nthr; t++) {
        close(fds[t]);
        unlink((dir + "/zil-full-" + std::to_string(t)).c_str());
    }

    if (ffd >= 0) {
        report(fsync(ffd) == 0 || errno == ENOSPC, "fsync the full fill file");
        close(ffd);
    }
    report(unlink(fill.c_str()) == 0, "remove the fill file on a full pool");
    // OpenZFS frees a removed file over several txgs; BSD ZFS at once.
    unsigned long long free1 = 0;
    int waited = 0;
    for (; waited < 60; waited++) {
        sync();
        progress++;
        free1 = free_bytes(dir);
        if (free1 + (64ULL << 20) >= free0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    printf("after remove: %llu MB free after %d s (was %llu MB)\n", free1 >> 20, waited,
           free0 >> 20);
    report(free1 + (64ULL << 20) >= free0, "space comes back after the remove");

    int fd = open(fill.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    bool ok = fd >= 0 && write(fd, chunk.data(), chunk.size()) == (ssize_t)chunk.size() &&
              fsync(fd) == 0;
    if (fd >= 0) {
        close(fd);
    }
    unlink(fill.c_str());
    report(ok, "write + fsync work again after the remove");

    done = true;
    watchdog.join();
    printf("refused=%lld (was %lld)\n", refused(), refused0);
    printf("SUMMARY: %d tests, %d failures\n", tests, failures);
    return failures ? 1 : 0;
}
