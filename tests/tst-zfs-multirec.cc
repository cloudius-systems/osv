/*
 * Copyright (C) 2026 OSv Authors
 *
 * Multi-record write + read regression test.
 *
 * Writes a file that spans more than one ZFS recordsize (default 128
 * KiB) in a single write() call, fsyncs, and reads back.  This is the
 * smallest workload that triggers ZFS to issue parallel per-record
 * Writes through the vdev_disk path.
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    /* /tmp is on the boot ZFS pool; this lets us verify the bug
     * appears (or not) on a pool backed by virtio-blk vs Crucible. */
    const char *path = "/tmp/multirec-test.bin";
    constexpr size_t N = 256 * 1024;     /* exactly 2 ZFS records */

    uint8_t *w = (uint8_t *)malloc(N);
    uint8_t *r = (uint8_t *)malloc(N);
    /* Simple monotonic pattern that makes the file content unique
     * per byte and easy to diagnose: byte at offset N = N % 251. */
    for (size_t i = 0; i < N; i++) {
        w[i] = (uint8_t)(i % 251);
    }

    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) { perror("open"); return 1; }
    if (write(fd, w, N) != (ssize_t)N) { perror("write"); return 1; }
    if (fsync(fd) != 0) { perror("fsync"); return 1; }
    close(fd);

    fd = open(path, O_RDONLY);
    if (fd < 0) { perror("re-open"); return 1; }
    if (read(fd, r, N) != (ssize_t)N) { perror("read"); return 1; }
    close(fd);
    unlink(path);

    if (memcmp(w, r, N) == 0) {
        printf("PASS: %zu-byte single-write round-trip OK\n", N);
        return 0;
    }
    size_t d = 0;
    for (size_t i = 0; i < N; i++) if (w[i] != r[i]) { d = i; break; }
    printf("FAIL: first diff at %zu (expected 0x%02x got 0x%02x)\n",
           d, w[d], r[d]);
    printf("  expected:");
    for (size_t i = d; i < d + 16 && i < N; i++) printf(" %02x", w[i]);
    printf("\n  got:     ");
    for (size_t i = d; i < d + 16 && i < N; i++) printf(" %02x", r[i]);
    printf("\n");
    return 1;
}
