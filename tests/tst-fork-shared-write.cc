/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for [W-sharedbuf]: the storage / shared-buffer coherence wall
 * PostgreSQL hits under a heavy bulk load (pgbench -i).
 *
 * A large ramfs file's segment DATA BUFFER is enlarged by malloc()
 * (fs/ramfs/ramfs_vnops.cc ramfs_enlarge_data_buffer).  When the segment grows
 * past the fork arena's max_alloc (2 MiB), the allocation falls through to
 * malloc_large()'s size>=huge_page && !contiguous branch, which reserves an
 * APP-SLOT (VA < 0x400000000000) anonymous mmap in the CURRENT process's
 * address space.  With per-child COW address spaces, that buffer lives ONLY in
 * the address space of the process that enlarged the file.  A sibling forked
 * process read()ing (or write()ing) the SAME ramfs file then runs
 * ramfs_read_or_write_file_data -> uiomove -> memcpy against that app-slot VA,
 * which is UNMAPPED in its own page tables:
 *
 *   page fault outside application, addr: 0x2000....
 *   RIP: memcpy_repmov_ssse3 ; pread/pwrite -> vfs_file::read/write -> ramfs
 *
 * i.e. the exact fault seen under `pgbench -i -s 5` (parallel index build /
 * checkpoint large write).  It is NOT a fork/reap lifecycle fault -- the file
 * data is served fine within one process -- it is a shared-storage buffer that
 * is not present in a sibling backend's address space.
 *
 * The fix (fs/ramfs/ramfs_vnops.cc) allocates the segment data buffer as
 * physically-contiguous IDENTITY-mapped memory (memory::alloc_phys_contiguous_
 * aligned, VA >= 0x400000000000, in the kernel PML4 slots clone_address_space()
 * shares verbatim), so every address space reaches the same file image.
 *
 * The test mirrors the pgbench-i pattern:
 *   parent creates a large file on the (ramfs) tmpfs;
 *   fork child A: write()s a large distinctive pattern across the file (forces
 *     ramfs_enlarge_data_buffer to allocate the big segment buffer in A's AS);
 *   fork child B (a DIFFERENT, already-forked sibling AS): read()s the whole
 *     file back and verifies A's pattern -- on HEAD this memcpy-faults on the
 *     buffer VA that is unmapped in B's AS;
 *   the parent also read()s it back.
 * Each child owns its verdict via its exit code; the parent waitpid-verifies.
 *
 * To reproduce (arena-dev, CONF_fork=y):
 *   ./scripts/build conf_fork=1 fs=ramfs image=tests, boot with -smp 2:
 *   qemu ... --rootfs=ramfs /tests/tst-fork-shared-write.so  (-smp 2)
 * (ramfs is writable at runtime; /tmp is ramfs on the tests image.)
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <cstdlib>

// Big enough that the ramfs segment buffer grows past the fork arena
// max_alloc (2 MiB) into the malloc_large app-slot path -- the fault trigger.
static const size_t FILE_SIZE = 8u * 1024 * 1024;   // 8 MiB
static const size_t CHUNK     = 64u * 1024;         // write/read granularity
static const char  *PATH      = "/tmp/tst-fork-shared-write.dat";

// Deterministic byte pattern so a reader can verify the writer's content
// exactly, catching both a fault (unmapped buffer) and silent divergence
// (a private, incoherent buffer copy).
static inline uint8_t pat(size_t off) { return (uint8_t)((off * 1103515245u + 12345u) >> 16); }

static int write_pattern(int fd)
{
    auto buf = (uint8_t *)malloc(CHUNK);
    if (!buf) return -1;
    for (size_t off = 0; off < FILE_SIZE; off += CHUNK) {
        for (size_t i = 0; i < CHUNK; i++) buf[i] = pat(off + i);
        ssize_t n = pwrite(fd, buf, CHUNK, off);
        if (n != (ssize_t)CHUNK) { free(buf); return -1; }
    }
    free(buf);
    return 0;
}

// Returns 0 if the whole file matches the pattern, else the failing offset+1.
static size_t verify_pattern(int fd)
{
    auto buf = (uint8_t *)malloc(CHUNK);
    if (!buf) return 1;
    for (size_t off = 0; off < FILE_SIZE; off += CHUNK) {
        // pread here is the read path that memcpy-faults on HEAD: it copies
        // FROM the ramfs segment buffer (an app-slot VA, unmapped in this
        // sibling AS) INTO buf.
        ssize_t n = pread(fd, buf, CHUNK, off);
        if (n != (ssize_t)CHUNK) { free(buf); return off + 1; }
        for (size_t i = 0; i < CHUNK; i++) {
            if (buf[i] != pat(off + i)) { free(buf); return off + i + 1; }
        }
    }
    free(buf);
    return 0;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-shared-write ===\n");

    unlink(PATH);
    // Parent creates the file and sizes it (still small; child A grows it).
    int fd = open(PATH, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) { printf("FAIL: parent open errno=%d\n", errno); return 1; }

    // ---- Child A: writer.  It ftruncate()s the file to the full size FIRST,
    // which makes ramfs_enlarge_data_buffer allocate ONE segment buffer of the
    // whole size (> the 2 MiB fork-arena max_alloc) -- forcing the buffer down
    // malloc_large's app-slot mmap path in A's address space on HEAD.  Then it
    // writes the pattern into that buffer. ----
    pid_t a = fork();
    if (a == 0) {
        if (ftruncate(fd, FILE_SIZE) < 0) { printf("FAIL(A): ftruncate errno=%d\n", errno); _exit(5); }
        int r = write_pattern(fd);
        if (r != 0) { printf("FAIL(A): write_pattern errno=%d\n", errno); _exit(2); }
        // A verifies its own write within its own AS (this always works -- it
        // is the CROSS-AS reader that faults on HEAD).
        if (verify_pattern(fd) != 0) { printf("FAIL(A): self-verify\n"); _exit(3); }
        printf("child A: wrote+verified %zu bytes\n", FILE_SIZE);
        _exit(0);
    }
    if (a < 0) { printf("FAIL: fork A errno=%d\n", errno); return 1; }

    int st = 0;
    if (waitpid(a, &st, 0) != a || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("FAIL: child A exit status=0x%x\n", st);
        return 1;
    }

    // ---- Child B: a DIFFERENT forked sibling that never wrote the file, so
    // it has no private mapping of the segment buffer.  Its read()s copy from
    // the buffer A allocated -> the cross-AS coherence path.  On HEAD this
    // page-faults ("page fault outside application" in ramfs read memcpy). ----
    pid_t b = fork();
    if (b == 0) {
        size_t bad = verify_pattern(fd);
        if (bad != 0) {
            printf("FAIL(B): mismatch/short at offset %zu -- ramfs segment "
                   "buffer not coherent across fork\n", bad - 1);
            _exit(4);
        }
        printf("child B: read back %zu bytes, all match A's pattern\n", FILE_SIZE);
        _exit(0);
    }
    if (b < 0) { printf("FAIL: fork B errno=%d\n", errno); return 1; }
    if (waitpid(b, &st, 0) != b || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("FAIL: child B exit status=0x%x (cross-AS ramfs read faulted "
               "or diverged)\n", st);
        return 1;
    }

    // ---- Parent (AS0) also reads it back. ----
    if (verify_pattern(fd) != 0) {
        printf("FAIL: parent read-back mismatch\n");
        return 1;
    }
    close(fd);
    unlink(PATH);

    printf("PASS: large ramfs file written by one forked child is coherent "
           "when read by a sibling child and the parent (shared-buffer/ramfs "
           "cross-AS coherence)\n");
    printf("tst-fork-shared-write done: 0 failures\n");
    return 0;
}
