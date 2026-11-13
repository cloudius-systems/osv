/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST: a forked child WRITES + fsync()s a file on a ZFS mount, and
 * the data must durably round-trip -- the parent (after reaping the child)
 * reads back exactly what the child wrote, including the file's grown size.
 *
 * This reproduces the two OSv walls that blocked stock PostgreSQL (a forked
 * backend architecture) from durably committing writes through OpenZFS:
 *
 *   WALL (B) [fork-stack / CONF_fork] -- forked-backend writes produced ZERO
 *     block I/O.  The ZFS txg_sync thread runs in AS0; issuing a write ZIO calls
 *     taskqueue_enqueue() which mtx_lock()s the zio taskqueue's tq_mutex.  That
 *     taskqueue was calloc()'d from the fork COW arena, so after the first
 *     fork() the page is COW-write-protected in AS0; the AS0 sync thread's
 *     atomic write to tq_mutex takes a COW write fault that grabs the kernel
 *     vma_list_mutex for write and deadlocks against the forked child holding
 *     it for read across a demand fault.  txg_sync wedged -> no writes reached
 *     the disk.  Fixed by routing taskqueue allocations onto the identity
 *     kernel heap (bsd/sys/kern/subr_taskqueue.c).
 *
 *   Read-after-write coherence [#1423 / OpenZFS] -- zfs_vop_write() updated
 *     zp->z_size but not the OSv vnode's cached vp->v_size, which
 *     zfs_vop_read()/zfs_vop_cache() use to bound reads, so a read of a
 *     just-written (file-extending) region returned EOF.  Fixed by refreshing
 *     vp->v_size from zp->z_size after write (patch 0028).
 *
 * On a buggy kernel the child's write either never persists (deadlock/hang) or
 * the parent's read-back sees a short/empty file (beyond-EOF) -> FAIL.  With
 * the fixes the parent reads the child's bytes at the grown size -> PASS.
 *
 * Gated CONF_fork: the whole scenario only makes sense with fork() enabled.
 * The file lives on /tmp, which on an fs=zfs test image is a real ZFS dataset
 * backed by virtio-blk (so this drives the actual ZFS write path).
 */

#include <osv/kernel_config_fork.h>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-zfs-write requires CONF_fork\n");
    return 0;
}

#else

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (errno=%d %s)\n", msg, errno, strerror(errno)); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

// A distinctive, multi-record payload so a stale/short read is obvious.
// 40 KiB spans several 8 KiB ZFS records (recordsize on the PG pool) and one
// 128 KiB default record, and is larger than the file's initial (zero) size so
// the write is file-extending -- exactly the case the v_size bug broke.
static constexpr size_t N = 40 * 1024;

int main()
{
    printf("=== tst-fork-zfs-write ===\n");

    const char *path = "/tmp/fork-zfs-write.bin";
    unlink(path);

    unsigned char *w = (unsigned char *)malloc(N);
    unsigned char *r = (unsigned char *)malloc(N);
    if (!w || !r) { printf("FAIL: malloc\n"); return 1; }
    for (size_t i = 0; i < N; i++)
        w[i] = (unsigned char)((i * 31 + 7) & 0xff);   // unique-ish per byte

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD: create, write the whole payload, fsync, close.  On a buggy
        // kernel the write path deadlocks (test times out) or persists nothing.
        int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd < 0) _exit(11);
        size_t off = 0;
        while (off < N) {
            ssize_t n = write(fd, w + off, N - off);
            if (n <= 0) { close(fd); _exit(12); }
            off += (size_t)n;
        }
        if (fsync(fd) != 0) { close(fd); _exit(13); }
        if (close(fd) != 0) _exit(14);
        _exit(0);
    }
    if (pid < 0) { printf("FAIL: fork (%s)\n", strerror(errno)); return 1; }

    int status = 0;
    pid_t rw = waitpid(pid, &status, 0);
    CHECK(rw == pid, "reaped the fork child");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "child wrote+fsync'd the file without error");
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        printf("     child exit status raw=%d\n", status);
    }

    // PARENT (post-reap, a DIFFERENT address space than the child): the file
    // the child wrote+fsync'd must be visible at full size with exact bytes.
    struct stat st;
    int sr = stat(path, &st);
    CHECK(sr == 0, "parent stat() sees the child's file");
    CHECK(sr == 0 && (size_t)st.st_size == N,
          "parent sees the grown file size (not beyond-EOF / short)");
    if (sr == 0 && (size_t)st.st_size != N)
        printf("     st_size=%lld expected=%zu\n", (long long)st.st_size, N);

    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0, "parent open() the child's file");
    if (fd >= 0) {
        size_t off = 0; int rok = 1;
        while (off < N) {
            ssize_t n = pread(fd, r + off, N - off, (off_t)off);
            if (n <= 0) { rok = 0; break; }
            off += (size_t)n;
        }
        CHECK(rok && off == N, "parent read back the full payload");
        CHECK(rok && off == N && memcmp(w, r, N) == 0,
              "parent read back EXACTLY the child's bytes (durable round-trip)");
        close(fd);
    }

    unlink(path);
    free(w); free(r);

    if (failures == 0) {
        printf("PASS: tst-fork-zfs-write -- forked-child ZFS write durably "
               "round-trips to the parent\n");
        return 0;
    }
    printf("FAIL: tst-fork-zfs-write -- %d checks failed\n", failures);
    return 1;
}

#endif // CONF_fork
