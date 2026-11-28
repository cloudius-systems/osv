/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Forked processes write more data to ZFS than the ARC can hold, so they
// block in arc_wait_for_eviction() until the ARC eviction thread frees
// space and signals them.  That thread runs in the global address space;
// if the waiter it signals lives on the forked process's stack (private to
// the child's copy-on-write address space) the signal touches the wrong
// copy: the guest faults in condvar::wake_one(), or the writer never wakes
// and the test times out.
//
// Usage: tst-fork-arc-evict.so [MiB per child] [children] [dir].  Run it in a
// guest with little memory (e.g. 1 GiB) so the ARC is small; the defaults
// write 4 x 256 MiB to /tmp.

#include <osv/kernel_config_fork.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-arc-evict requires CONF_fork\n");
    return 0;
}

#else

static const size_t chunk = 128 * 1024;

static int writer(const char *path, size_t mib, int id)
{
    char *buf = static_cast<char*>(malloc(chunk));
    if (!buf) {
        return 11;
    }
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        return 12;
    }
    for (size_t off = 0; off < mib << 20; off += chunk) {
        for (size_t i = 0; i < chunk; i += 4096) {
            snprintf(buf + i, 32, "%d:%zu", id, off + i);
        }
        if (write(fd, buf, chunk) != (ssize_t)chunk) {
            return 13;
        }
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        return 14;
    }
    return 0;
}

static bool verify(const char *path, size_t mib, int id)
{
    char *buf = static_cast<char*>(malloc(chunk));
    char want[32];
    int fd = open(path, O_RDONLY);
    if (!buf || fd < 0) {
        return false;
    }
    bool ok = true;
    for (size_t off = 0; ok && off < mib << 20; off += chunk) {
        ok = read(fd, buf, chunk) == (ssize_t)chunk;
        for (size_t i = 0; ok && i < chunk; i += 4096) {
            snprintf(want, sizeof(want), "%d:%zu", id, off + i);
            ok = strcmp(buf + i, want) == 0;
        }
    }
    close(fd);
    free(buf);
    return ok;
}

int main(int argc, char **argv)
{
    size_t mib = argc > 1 ? strtoul(argv[1], nullptr, 0) : 256;
    int n = argc > 2 ? atoi(argv[2]) : 4;
    const char *dir = argc > 3 ? argv[3] : "/tmp";
    char path[n][128];
    pid_t pid[n];
    int failures = 0;

    for (int i = 0; i < n; i++) {
        snprintf(path[i], sizeof(path[i]), "%s/arc-evict-%d.bin", dir, i);
        pid[i] = fork();
        if (pid[i] == 0) {
            _exit(writer(path[i], mib, i));
        }
        if (pid[i] < 0) {
            printf("FAIL: fork: %s\n", strerror(errno));
            return 1;
        }
    }
    for (int i = 0; i < n; i++) {
        int status = 0;
        if (waitpid(pid[i], &status, 0) != pid[i] || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            printf("FAIL: writer %d status 0x%x\n", i, status);
            failures++;
        } else if (!verify(path[i], mib, i)) {
            printf("FAIL: writer %d data does not read back\n", i);
            failures++;
        }
        unlink(path[i]);
    }
    printf("%d writers x %zu MiB, %d failures\n", n, mib, failures);
    if (failures == 0) {
        printf("PASS\n");
    }
    return failures ? 1 : 0;
}

#endif
