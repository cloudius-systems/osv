/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Check that removing a file gives its space back to the file system.
//
// Each case writes a file of SIZE MiB, fsyncs it, drops it in one way
// (unlink after close, unlink while open then close, rename over it,
// truncate to 0, unlink while mmap'd, written and closed by a forked child),
// and then polls statvfs() with sync()
// until the free space is back within a small slack of the value before the
// write.  ZFS frees blocks in the syncing txg, so the space comes back within
// a txg or two; a case fails if it has not come back after WAIT seconds.
//
// Usage: tst-zfs-unlink-space.so [-d DIR] [-s SIZE_MIB] [-n CYCLES] [-w WAIT_S] [-v] [-A]
// With -v the free space is printed after every cycle (the diagnostic mode).
// With -A it writes a file, unlinks it while open and calls abort(), so the
// file is still in the unlinked set at the next mount; a following boot shows
// whether the mount's drain freed it.
// The default (64 MiB, 1 cycle, 60 s) runs in the tests image.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

static std::string dir = "/";
static long size_mib = 64;
static int cycles = 1;
static int wait_s = 60;
static bool verbose;
static int failures;

static long long free_bytes()
{
    struct statvfs st;
    if (statvfs(dir.c_str(), &st) != 0) {
        perror("statvfs");
        exit(1);
    }
    return (long long)st.f_bavail * st.f_frsize;
}

// Write size_mib MiB of non-zero data (so compression cannot hide it) to a
// new file, fsync it, and return the open descriptor.
static int write_file(const std::string& path)
{
    int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        perror(path.c_str());
        exit(1);
    }
    static char buf[1 << 20];
    for (long i = 0; i < size_mib; i++) {
        for (size_t j = 0; j < sizeof(buf); j += 8) {
            unsigned long long v = (i << 32) ^ j ^ 0x9e3779b97f4a7c15ULL * (j + 1);
            memcpy(buf + j, &v, 8);
        }
        if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
            perror("write");
            exit(1);
        }
    }
    if (fsync(fd) != 0) {
        perror("fsync");
        exit(1);
    }
    return fd;
}

// Wait up to wait_s seconds for the free space to come back to within 1/8 of
// the file size of `before`.  Returns the seconds it took, or -1.
static int wait_for_space(long long before)
{
    long long slack = size_mib * (1 << 20) / 8;
    for (int t = 0; t <= wait_s; t++) {
        sync();
        if (free_bytes() >= before - slack) {
            return t;
        }
        sleep(1);
    }
    return -1;
}

static void drop_unlink(const std::string& p)
{
    close(write_file(p));
    unlink(p.c_str());
}

static void drop_unlink_open(const std::string& p)
{
    int fd = write_file(p);
    unlink(p.c_str());
    close(fd);
}

static void drop_rename_over(const std::string& p)
{
    close(write_file(p));
    std::string small = p + ".new";
    int fd = open(small.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    close(fd);
    rename(small.c_str(), p.c_str());
    unlink(p.c_str());
}

static void drop_truncate(const std::string& p)
{
    int fd = write_file(p);
    if (ftruncate(fd, 0) != 0) {
        perror("ftruncate");
    }
    close(fd);
    unlink(p.c_str());
}

static void drop_mmap(const std::string& p)
{
    int fd = write_file(p);
    size_t len = size_mib << 20;
    void* m = mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    volatile char c = ((char*)m)[len - 1];
    (void)c;
    close(fd);
    unlink(p.c_str());
    munmap(m, len);
}

// The child inherits the open file, and the parent closes and unlinks it
// first, so the child's close and exit drop the last reference.  Without
// fork() (ENOSYS) the parent does it all, the same as drop_unlink_open.
static void drop_fork_child(const std::string& p)
{
    int fd = write_file(p);
    int go[2];
    if (pipe(go) != 0) {
        perror("pipe");
        exit(1);
    }
    pid_t pid = fork();
    if (pid == 0) {
        char c;
        close(go[1]);
        (void)read(go[0], &c, 1);
        close(fd);
        _exit(0);
    }
    unlink(p.c_str());
    close(fd);
    close(go[0]);
    close(go[1]);
    if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

static void run_case(const char* name, void (*drop)(const std::string&))
{
    std::string p = dir + (dir.back() == '/' ? "" : "/") + "tst-zfs-unlink-space." + name;
    long long start = free_bytes();
    int worst = 0;
    for (int i = 0; i < cycles; i++) {
        long long before = free_bytes();
        drop(p);
        int t = wait_for_space(before);
        if (verbose) {
            printf("%s cycle %d: free %lld MiB -> %lld MiB, back in %d s\n",
                   name, i, before >> 20, free_bytes() >> 20, t);
        }
        if (t < 0) {
            worst = -1;
            break;
        }
        worst = t > worst ? t : worst;
    }
    long long lost = start - free_bytes();
    if (worst < 0) {
        printf("FAIL %s: %lld MiB not returned after %d s\n", name, lost >> 20, wait_s);
        failures++;
    } else {
        printf("PASS %s: space returned within %d s (net %lld MiB)\n", name, worst, lost >> 20);
    }
}

int main(int argc, char** argv)
{
    int opt;
    bool crash = false;
    while ((opt = getopt(argc, argv, "d:s:n:w:vA")) != -1) {
        switch (opt) {
        case 'd': dir = optarg; break;
        case 's': size_mib = atol(optarg); break;
        case 'n': cycles = atoi(optarg); break;
        case 'w': wait_s = atoi(optarg); break;
        case 'v': verbose = true; break;
        case 'A': crash = true; break;
        default:
            fprintf(stderr, "usage: %s [-d dir] [-s MiB] [-n cycles] [-w s] [-v] [-A]\n", argv[0]);
            return 2;
        }
    }
    printf("dir %s, %ld MiB, %d cycles, free %lld MiB\n", dir.c_str(), size_mib, cycles,
           free_bytes() >> 20);
    if (crash) {
        std::string p = dir + (dir.back() == '/' ? "" : "/") + "tst-zfs-unlink-space.crash";
        int fd = write_file(p);
        unlink(p.c_str());
        // Commit the unlink so the object is on disk in the unlinked set.
        sync();
        printf("unlinked open fd %d, free %lld MiB; aborting\n", fd, free_bytes() >> 20);
        fflush(stdout);
        abort();
    }
    run_case("unlink", drop_unlink);
    run_case("unlink-open", drop_unlink_open);
    run_case("rename-over", drop_rename_over);
    run_case("truncate", drop_truncate);
    run_case("mmap", drop_mmap);
    run_case("fork-child", drop_fork_child);
    printf("free at end %lld MiB\n", free_bytes() >> 20);
    if (failures) {
        printf("FAIL: %d case(s)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
