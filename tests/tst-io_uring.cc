/*
 * Copyright (C) 2026 OSv Developers
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Unit tests for OSv's io_uring implementation.  The test drives the rings
 * directly through the mmap'd SQ/CQ memory and the io_uring_setup/enter/
 * register syscalls, so it exercises the same ABI a real io_uring user would.
 *
 * This test currently includes <osv/io_uring.h> for the ring-structure and
 * syscall-number declarations, so it builds only on OSv.  To run the same
 * cases on Linux for a cross-implementation confidence check, swap that
 * include for the system <linux/io_uring.h> (Linux >= 5.1) and build with:
 *     g++ -std=c++11 -o tst-io_uring tst-io_uring.cc
 * The ring layout and syscall semantics match the Linux ABI, so the test
 * bodies are otherwise portable.
 */

#include <osv/io_uring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <assert.h>

/* Test helper to work with current io_uring implementation limitations */
struct test_ring {
    int fd;
    struct io_uring_params params;
    struct io_uring_sq_ring *sq_ring;
    struct io_uring_cq_ring *cq_ring;
    struct io_uring_sqe *sqes;
    size_t sq_size;
    size_t cq_size;
    size_t sqe_size;
};

static int test_ring_init(struct test_ring *ring, unsigned entries)
{
    memset(ring, 0, sizeof(*ring));

    ring->fd = sys_io_uring_setup(entries, &ring->params);
    if (ring->fd < 0) {
        return ring->fd;
    }

    /* Calculate mmap sizes */
    ring->sq_size = sizeof(struct io_uring_sq_ring) +
                    ring->params.sq_entries * sizeof(uint32_t);
    ring->cq_size = sizeof(struct io_uring_cq_ring) +
                    ring->params.cq_entries * sizeof(struct io_uring_cqe);
    ring->sqe_size = ring->params.sq_entries * sizeof(struct io_uring_sqe);

    /* Note: mmap implementation is COMPLETE and functional.
     * All three regions (SQ ring, CQ ring, SQE array) are properly mapped.
     * Known limitations:
     * - Large page (2MB) support available but not fully tested
     * - io_uring_register() implemented for buffers and files
     * - Thread-per-op model (not thread pool) - acceptable for current use
     */

    /* Try to mmap SQ ring (offset 0) */
    ring->sq_ring = (struct io_uring_sq_ring*)mmap(NULL, ring->sq_size,
                                                     PROT_READ | PROT_WRITE,
                                                     MAP_SHARED, ring->fd, 0);
    if (ring->sq_ring == MAP_FAILED) {
        close(ring->fd);
        return -errno;
    }

    /* Try to mmap CQ ring (offset != 0) */
    ring->cq_ring = (struct io_uring_cq_ring*)mmap(NULL, ring->cq_size,
                                                     PROT_READ | PROT_WRITE,
                                                     MAP_SHARED, ring->fd, 0x8000000ULL);
    if (ring->cq_ring == MAP_FAILED) {
        munmap(ring->sq_ring, ring->sq_size);
        close(ring->fd);
        return -errno;
    }

    /* Try to mmap SQE array (offset 0x10000000) */
    ring->sqes = (struct io_uring_sqe*)mmap(NULL, ring->sqe_size,
                                             PROT_READ | PROT_WRITE,
                                             MAP_SHARED, ring->fd, 0x10000000ULL);
    if (ring->sqes == MAP_FAILED) {
        munmap(ring->cq_ring, ring->cq_size);
        munmap(ring->sq_ring, ring->sq_size);
        close(ring->fd);
        return -errno;
    }

    return 0;
}

static void test_ring_cleanup(struct test_ring *ring)
{
    if (ring->sqes && ring->sqes != MAP_FAILED) {
        munmap(ring->sqes, ring->sqe_size);
    }
    if (ring->sq_ring && ring->sq_ring != MAP_FAILED) {
        munmap(ring->sq_ring, ring->sq_size);
    }
    if (ring->cq_ring && ring->cq_ring != MAP_FAILED) {
        munmap(ring->cq_ring, ring->cq_size);
    }
    if (ring->fd >= 0) {
        close(ring->fd);
    }
}

/* -------------------------------------------------------------------------
 * Submit a single already-filled SQE (at the current tail) and reap exactly
 * one CQE, returning its res.  *out_flags (if non-null) receives cqe->flags.
 * The SQE must have been written into ring->sqes[tail & mask] and its
 * user_data set by the caller; this helper advances the SQ tail, enters, and
 * consumes one CQE, asserting user_data round-trips.
 * ---------------------------------------------------------------------- */
static int submit_reap_one(struct test_ring *ring, uint64_t user_data,
                           uint32_t *out_flags)
{
    unsigned tail  = ring->sq_ring->tail;
    unsigned index = tail & ring->sq_ring->ring_mask;
    ring->sqes[index].user_data = user_data;
    ring->sq_ring->tail = tail + 1;

    int ret = sys_io_uring_enter(ring->fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
    if (ret != 1)
        return -1000000 + ret;   /* sentinel: enter did not submit exactly one */

    unsigned cq_head = ring->cq_ring->head;
    if (ring->cq_ring->tail == cq_head)
        return -2000000;         /* sentinel: no CQE posted */

    struct io_uring_cqe *cqe =
        &ring->cq_ring->cqes[cq_head & ring->cq_ring->ring_mask];
    assert(cqe->user_data == user_data);
    int res = cqe->res;
    if (out_flags)
        *out_flags = cqe->flags;
    ring->cq_ring->head = cq_head + 1;
    return res;
}

/* Fill the SQE at the current tail, zeroed, and return a pointer to it. */
static struct io_uring_sqe *next_sqe(struct test_ring *ring)
{
    unsigned index = ring->sq_ring->tail & ring->sq_ring->ring_mask;
    struct io_uring_sqe *sqe = &ring->sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    return sqe;
}

/* A3-1/A3-4: modern setup flags PostgreSQL uses must be accepted; geometry-
 * changing flags that break the mmap contract must be honestly rejected. */
static void test_io_uring_setup_flags(void)
{
    printf("Testing io_uring setup flag acceptance/rejection...\n");

    /* DEFER_TASKRUN|SINGLE_ISSUER = modern liburing/PG default: must succeed. */
    struct io_uring_params p = {};
    p.flags = IORING_SETUP_DEFER_TASKRUN | IORING_SETUP_SINGLE_ISSUER;
    int fd = sys_io_uring_setup(8, &p);
    assert(fd >= 0);
    close(fd);

    /* SQE128 changes SQE size -> breaks our fixed mmap geometry: reject. */
    struct io_uring_params p2 = {};
    p2.flags = IORING_SETUP_SQE128;
    int fd2 = sys_io_uring_setup(8, &p2);
    assert(fd2 == -EINVAL);

    /* CQE32 likewise. */
    struct io_uring_params p3 = {};
    p3.flags = IORING_SETUP_CQE32;
    int fd3 = sys_io_uring_setup(8, &p3);
    assert(fd3 == -EINVAL);

    printf("  PASSED - setup flags accepted/rejected per honesty rule\n");
}

/* A3-8: only implemented FEAT bits may be advertised. */
static void test_io_uring_features(void)
{
    printf("Testing advertised feature bits...\n");

    struct io_uring_params p = {};
    int fd = sys_io_uring_setup(8, &p);
    assert(fd >= 0);

    /* These are backed by real behavior. */
    assert(p.features & IORING_FEAT_NODROP);          /* A2-4 backlog */
    assert(p.features & IORING_FEAT_SUBMIT_STABLE);
    assert(p.features & IORING_FEAT_NATIVE_WORKERS);  /* A2-1 io-wq pool */
    assert(p.features & IORING_FEAT_CQE_SKIP);        /* CQE_SKIP_SUCCESS */

    /* SINGLE_MMAP is deliberately NOT advertised (3 separate regions). */
    assert(!(p.features & IORING_FEAT_SINGLE_MMAP));

    close(fd);
    printf("  PASSED - feature bits honest\n");
}

/* A3: R_DISABLED ring rejects submissions with -EBADFD until ENABLE_RINGS. */
static void test_io_uring_disabled_ring(void)
{
    printf("Testing R_DISABLED + ENABLE_RINGS...\n");

    struct test_ring ring;
    memset(&ring, 0, sizeof(ring));
    ring.fd = sys_io_uring_setup(8, &ring.params);
    /* R_DISABLED needs the ring set up disabled: re-open with the flag. */
    close(ring.fd);

    struct io_uring_params p = {};
    p.flags = IORING_SETUP_R_DISABLED;
    int fd = sys_io_uring_setup(8, &p);
    assert(fd >= 0);

    /* map SQ ring + SQE array so we can submit */
    size_t sq_size = sizeof(struct io_uring_sq_ring) +
                     p.sq_entries * sizeof(uint32_t);
    size_t sqe_size = p.sq_entries * sizeof(struct io_uring_sqe);
    struct io_uring_sq_ring *sq = (struct io_uring_sq_ring *)mmap(
        NULL, sq_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(sq != MAP_FAILED);
    struct io_uring_sqe *sqes = (struct io_uring_sqe *)mmap(
        NULL, sqe_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x10000000ULL);
    assert(sqes != MAP_FAILED);

    unsigned index = sq->tail & sq->ring_mask;
    memset(&sqes[index], 0, sizeof(sqes[index]));
    sqes[index].opcode = IORING_OP_NOP;
    sq->tail = sq->tail + 1;

    /* While disabled, submit must be rejected with -EBADFD. */
    int ret = sys_io_uring_enter(fd, 1, 0, 0, NULL, 0);
    assert(ret == -EBADFD);

    /* Enable the ring. */
    ret = sys_io_uring_register(fd, IORING_REGISTER_ENABLE_RINGS, NULL, 0);
    assert(ret == 0);

    /* Now the submission goes through. */
    ret = sys_io_uring_enter(fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
    assert(ret == 1);

    munmap(sqes, sqe_size);
    munmap(sq, sq_size);
    close(fd);
    printf("  PASSED - disabled ring gates submissions\n");
}

/* A3-6: IORING_REGISTER_IOWQ_MAX_WORKERS returns previous caps in place. */
static void test_io_uring_iowq_max_workers(void)
{
    printf("Testing IOWQ_MAX_WORKERS register...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    uint32_t vals[2] = { 0, 0 };   /* 0 = "leave unchanged, report current" */
    ret = sys_io_uring_register(ring.fd, IORING_REGISTER_IOWQ_MAX_WORKERS,
                                vals, 2);
    assert(ret == 0);
    /* Non-zero defaults must have been reported back. */
    assert(vals[0] > 0);           /* bounded */
    assert(vals[1] > 0);           /* unbounded */

    /* Set new caps and confirm the previous ones are returned. */
    uint32_t set[2] = { 3, 7 };
    ret = sys_io_uring_register(ring.fd, IORING_REGISTER_IOWQ_MAX_WORKERS,
                                set, 2);
    assert(ret == 0);
    /* set[] now holds the PREVIOUS values (the defaults from above). */
    assert(set[0] == vals[0]);
    assert(set[1] == vals[1]);

    test_ring_cleanup(&ring);
    printf("  PASSED - IOWQ_MAX_WORKERS reports prior caps\n");
}

/* Axis-1 opcode 55: FTRUNCATE. */
static void test_io_uring_ftruncate(void)
{
    printf("Testing FTRUNCATE opcode...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    const char *path = "/tmp/io_uring_ftrunc.bin";
    int tfd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(tfd >= 0);

    struct io_uring_sqe *sqe = next_sqe(&ring);
    sqe->opcode = IORING_OP_FTRUNCATE;
    sqe->fd = tfd;
    sqe->off = 4096;   /* new length */

    int res = submit_reap_one(&ring, 0x77, NULL);
    assert(res == 0);

    struct stat st;
    assert(fstat(tfd, &st) == 0);
    assert(st.st_size == 4096);

    close(tfd);
    unlink(path);
    test_ring_cleanup(&ring);
    printf("  PASSED - FTRUNCATE sets file length\n");
}

/* A2-10: SPLICE copies bytes between two regular files through the VFS. */
static void test_io_uring_splice(void)
{
    printf("Testing SPLICE opcode (file->file copy)...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    const char *src_path = "/tmp/io_uring_splice_src.bin";
    const char *dst_path = "/tmp/io_uring_splice_dst.bin";
    int src = open(src_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int dst = open(dst_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(src >= 0 && dst >= 0);

    const char payload[] = "splice-me-through-the-vfs-layer";
    size_t plen = sizeof(payload) - 1;
    assert(write(src, payload, plen) == (ssize_t)plen);
    lseek(src, 0, SEEK_SET);

    struct io_uring_sqe *sqe = next_sqe(&ring);
    sqe->opcode = IORING_OP_SPLICE;
    sqe->splice_fd_in = src;
    sqe->splice_off_in = 0;         /* explicit source offset */
    sqe->fd = dst;
    sqe->off = 0;                   /* explicit dest offset */
    sqe->len = plen;

    int res = submit_reap_one(&ring, 0x99, NULL);
    assert(res == (int)plen);

    char buf[64] = {0};
    lseek(dst, 0, SEEK_SET);
    assert(read(dst, buf, plen) == (ssize_t)plen);
    assert(memcmp(buf, payload, plen) == 0);

    close(src);
    close(dst);
    unlink(src_path);
    unlink(dst_path);
    test_ring_cleanup(&ring);
    printf("  PASSED - SPLICE copies file->file\n");
}

/* A2-11: SYNC_FILE_RANGE validates its arguments (unknown flag -> -EINVAL). */
static void test_io_uring_sync_file_range(void)
{
    printf("Testing SYNC_FILE_RANGE argument validation...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    const char *path = "/tmp/io_uring_sfr.bin";
    int tfd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(tfd >= 0);
    assert(write(tfd, "data", 4) == 4);

    /* Bogus flag bit must be rejected. */
    struct io_uring_sqe *sqe = next_sqe(&ring);
    sqe->opcode = IORING_OP_SYNC_FILE_RANGE;
    sqe->fd = tfd;
    sqe->off = 0;
    sqe->len = 4;
    sqe->sync_range_flags = 0x8000;   /* not a valid SYNC_FILE_RANGE_* bit */
    int res = submit_reap_one(&ring, 0xAA, NULL);
    assert(res == -EINVAL);

    /* Valid full-file sync (off=0,len=0 = to EOF) succeeds. */
    sqe = next_sqe(&ring);
    sqe->opcode = IORING_OP_SYNC_FILE_RANGE;
    sqe->fd = tfd;
    sqe->off = 0;
    sqe->len = 0;
    sqe->sync_range_flags = SYNC_FILE_RANGE_WRITE;
    res = submit_reap_one(&ring, 0xAB, NULL);
    assert(res == 0);

    close(tfd);
    unlink(path);
    test_ring_cleanup(&ring);
    printf("  PASSED - SYNC_FILE_RANGE validates args\n");
}

/* A2-9: READ_FIXED must bounds-check the target against the registered buffer
 * length; an over-length read into a registered buffer returns -EFAULT. */
static void test_io_uring_fixed_buffer_bounds(void)
{
    printf("Testing READ_FIXED/WRITE_FIXED bounds check...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Register a single 512-byte buffer. */
    static char regbuf[512];
    struct iovec iov = { regbuf, sizeof(regbuf) };
    ret = sys_io_uring_register(ring.fd, IORING_REGISTER_BUFFERS, &iov, 1);
    assert(ret == 0);

    const char *path = "/tmp/io_uring_fixed.bin";
    int tfd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(tfd >= 0);
    assert(write(tfd, regbuf, sizeof(regbuf)) == (ssize_t)sizeof(regbuf));

    /* WRITE_FIXED with len exceeding the registered buffer -> -EFAULT. */
    struct io_uring_sqe *sqe = next_sqe(&ring);
    sqe->opcode = IORING_OP_WRITE_FIXED;
    sqe->fd = tfd;
    sqe->addr = (uint64_t)regbuf;
    sqe->len = sizeof(regbuf) + 1;    /* one byte past the region */
    sqe->off = 0;
    sqe->buf_index = 0;
    int res = submit_reap_one(&ring, 0xBB, NULL);
    assert(res == -EFAULT);

    /* In-bounds WRITE_FIXED succeeds. */
    sqe = next_sqe(&ring);
    sqe->opcode = IORING_OP_WRITE_FIXED;
    sqe->fd = tfd;
    sqe->addr = (uint64_t)regbuf;
    sqe->len = sizeof(regbuf);
    sqe->off = 0;
    sqe->buf_index = 0;
    res = submit_reap_one(&ring, 0xBC, NULL);
    assert(res == (int)sizeof(regbuf));

    close(tfd);
    unlink(path);
    test_ring_cleanup(&ring);
    printf("  PASSED - fixed-buffer bounds enforced\n");
}

/* Axis-1 opcode 40: MSG_RING (IORING_MSG_DATA) posts a CQE on a target ring. */
static void test_io_uring_msg_ring(void)
{
    printf("Testing MSG_RING (data) cross-ring post...\n");

    struct test_ring src, dst;
    assert(test_ring_init(&src, 8) == 0);
    assert(test_ring_init(&dst, 8) == 0);

    struct io_uring_sqe *sqe = next_sqe(&src);
    sqe->opcode = IORING_OP_MSG_RING;
    sqe->fd = dst.fd;                 /* target ring fd */
    sqe->addr = IORING_MSG_DATA;      /* command */
    sqe->len = 0x1234;                /* becomes target CQE res */
    sqe->off = 0xCAFEF00D;            /* becomes target CQE user_data */

    /* The source CQE reports success of the post itself. */
    int res = submit_reap_one(&src, 0xD0, NULL);
    assert(res == 0);

    /* Drain the target ring: a CQE with our injected res/user_data. */
    int ent = sys_io_uring_enter(dst.fd, 0, 1, IORING_ENTER_GETEVENTS, NULL, 0);
    assert(ent == 0);
    unsigned h = dst.cq_ring->head;
    assert(dst.cq_ring->tail != h);
    struct io_uring_cqe *tcqe = &dst.cq_ring->cqes[h & dst.cq_ring->ring_mask];
    assert(tcqe->user_data == 0xCAFEF00D);
    assert(tcqe->res == 0x1234);
    dst.cq_ring->head = h + 1;

    test_ring_cleanup(&src);
    test_ring_cleanup(&dst);
    printf("  PASSED - MSG_RING posts to target ring\n");
}

/* A2-12: io_uring_enter EXT_ARG timeout returns -ETIME when no CQEs arrive. */
static void test_io_uring_enter_ext_arg_timeout(void)
{
    printf("Testing enter EXT_ARG timeout (-ETIME)...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Wait for 1 completion that will never come, with a short timeout. */
    struct __kernel_timespec ts = { 0, 20 * 1000 * 1000 };   /* 20 ms */
    struct io_uring_getevents_arg arg = {};
    arg.ts = (uint64_t)(uintptr_t)&ts;

    ret = sys_io_uring_enter(ring.fd, 0, 1,
                             IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG,
                             &arg, sizeof(arg));
    assert(ret == -ETIME);

    /* REGISTERED_RING enter flag must be rejected (-EINVAL). */
    ret = sys_io_uring_enter(ring.fd, 0, 0, IORING_ENTER_REGISTERED_RING,
                             NULL, 0);
    assert(ret == -EINVAL);

    test_ring_cleanup(&ring);
    printf("  PASSED - EXT_ARG timeout + REGISTERED_RING reject\n");
}



static void test_io_uring_setup(void)
{
    printf("Testing io_uring_setup...\n");

    struct io_uring_params params = {};
    long fd = sys_io_uring_setup(32, &params);

    assert(fd >= 0);
    assert(params.sq_entries == 32);
    assert(params.cq_entries == 64);  /* Default is 2x SQ entries */

    close(fd);
    printf("  PASSED - setup syscall works\n");
}

static void test_io_uring_init(void)
{
    printf("Testing io_uring initialization...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Verify ring parameters were set correctly */
    assert(ring.params.sq_entries == 8);
    assert(ring.params.cq_entries == 16);

    /* Verify mmap succeeded (even if functionality is limited) */
    assert(ring.sq_ring != NULL && ring.sq_ring != MAP_FAILED);
    assert(ring.cq_ring != NULL && ring.cq_ring != MAP_FAILED);

    /* Verify ring metadata is initialized */
    assert(ring.sq_ring->ring_entries == 8);
    assert(ring.sq_ring->ring_mask == 7);
    assert(ring.cq_ring->ring_entries == 16);
    assert(ring.cq_ring->ring_mask == 15);

    test_ring_cleanup(&ring);
    printf("  PASSED - ring initialization works\n");
}

static void test_io_uring_enter_basic(void)
{
    printf("Testing io_uring_enter syscall...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Call io_uring_enter with no submissions: exercises the syscall
     * interface and the empty-submit fast path. */
    ret = sys_io_uring_enter(ring.fd, 0, 0, 0, NULL, 0);
    assert(ret == 0);

    test_ring_cleanup(&ring);
    printf("  PASSED - enter syscall works\n");
}

static void test_io_uring_register_buffers(void)
{
    printf("Testing io_uring_register syscall with buffers...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Test registering buffers */
    struct iovec bufs[2];
    char buf1[256], buf2[512];
    bufs[0].iov_base = buf1;
    bufs[0].iov_len = sizeof(buf1);
    bufs[1].iov_base = buf2;
    bufs[1].iov_len = sizeof(buf2);

    ret = sys_io_uring_register(ring.fd, IORING_REGISTER_BUFFERS, bufs, 2);
    assert(ret == 0);

    /* Test unregistering buffers */
    ret = sys_io_uring_register(ring.fd, IORING_UNREGISTER_BUFFERS, NULL, 0);
    assert(ret == 0);

    test_ring_cleanup(&ring);
    printf("  PASSED - buffer registration works\n");
}

static void test_io_uring_register_files(void)
{
    printf("Testing io_uring_register syscall with files...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Create temporary files */
    int test_fd1 = open("/tmp/io_uring_test1.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(test_fd1 >= 0);

    int test_fd2 = open("/tmp/io_uring_test2.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(test_fd2 >= 0);

    int fds[2] = { test_fd1, test_fd2 };

    /* Test registering files */
    ret = sys_io_uring_register(ring.fd, IORING_REGISTER_FILES, fds, 2);
    assert(ret == 0);

    /* Test unregistering files */
    ret = sys_io_uring_register(ring.fd, IORING_UNREGISTER_FILES, NULL, 0);
    assert(ret == 0);

    close(test_fd1);
    close(test_fd2);
    unlink("/tmp/io_uring_test1.txt");
    unlink("/tmp/io_uring_test2.txt");

    test_ring_cleanup(&ring);
    printf("  PASSED - file registration works\n");
}

static void test_io_uring_multiple_rings(void)
{
    printf("Testing multiple io_uring instances...\n");

    struct test_ring ring1, ring2;

    int ret1 = test_ring_init(&ring1, 8);
    assert(ret1 == 0);

    int ret2 = test_ring_init(&ring2, 16);
    assert(ret2 == 0);

    /* Verify both rings have correct parameters */
    assert(ring1.params.sq_entries == 8);
    assert(ring2.params.sq_entries == 16);
    assert(ring1.fd != ring2.fd);

    test_ring_cleanup(&ring1);
    test_ring_cleanup(&ring2);
    printf("  PASSED - multiple rings work\n");
}

static void test_io_uring_invalid_params(void)
{
    printf("Testing io_uring error handling...\n");

    struct io_uring_params params = {};

    /* Test entries too small */
    long fd = sys_io_uring_setup(0, &params);
    assert(fd == -EINVAL);

    /* Test entries too large */
    fd = sys_io_uring_setup(10000, &params);
    assert(fd == -EINVAL);

    /* Test NULL params */
    fd = sys_io_uring_setup(32, NULL);
    assert(fd == -EFAULT);

    /* SQE128 requires 128-byte SQE ring geometry we do not provide: rejected */
    params.flags = IORING_SETUP_SQE128;
    fd = sys_io_uring_setup(32, &params);
    assert(fd == -EINVAL);

    printf("  PASSED - error handling works\n");
}

static void test_io_uring_params_offsets(void)
{
    printf("Testing io_uring_params sq_off/cq_off are populated...\n");

    struct io_uring_params params = {};
    long fd = sys_io_uring_setup(8, &params);
    assert(fd >= 0);

    /* sq_off must point into the SQ ring mmap region */
    assert(params.sq_off.head         == 0);
    assert(params.sq_off.tail         == 4);
    assert(params.sq_off.ring_mask    == 8);
    assert(params.sq_off.ring_entries == 12);
    assert(params.sq_off.array        > 0);   /* array follows the header */

    /* cq_off must point into the CQ ring mmap region */
    assert(params.cq_off.head         == 0);
    assert(params.cq_off.tail         == 4);
    assert(params.cq_off.ring_mask    == 8);
    assert(params.cq_off.ring_entries == 12);
    assert(params.cq_off.cqes         > 0);   /* cqes follow the header */

    close(fd);
    printf("  PASSED - sq_off/cq_off populated correctly\n");
}

static void test_io_uring_enter_return_value(void)
{
    printf("Testing io_uring_enter return value...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Submit a NOP and verify enter returns the submitted count (1), not 0 */
    unsigned int tail = ring.sq_ring->tail;
    unsigned int index = tail & ring.sq_ring->ring_mask;
    struct io_uring_sqe *sqe = &ring.sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_NOP;
    sqe->user_data = 0xABCD;
    ring.sq_ring->tail = tail + 1;

    ret = sys_io_uring_enter(ring.fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
    assert(ret == 1);  /* must return number of submitted SQEs */

    /* Consume the CQE */
    unsigned cq_head = ring.cq_ring->head;
    ring.cq_ring->head = cq_head + 1;

    test_ring_cleanup(&ring);
    printf("  PASSED - enter returns submitted count\n");
}

static void test_io_uring_syscall_dispatch(void)
{
    printf("Testing io_uring via syscall() dispatch path...\n");

    struct io_uring_params params = {};
    /* Use the real syscall() path to verify kernel dispatch is wired up */
    long fd = syscall(SYS_io_uring_setup, 8, &params);
    assert(fd >= 0);
    assert(params.sq_entries == 8);
    assert(params.sq_off.array > 0);   /* offsets must be populated */

    /* Enter with nothing to submit */
    int ret = syscall(SYS_io_uring_enter, (int)fd, 0, 0, 0, NULL, 0);
    assert(ret == 0);

    close(fd);
    printf("  PASSED - syscall() dispatch path works\n");
}

static void test_io_uring_fd_operations(void)
{
    printf("Testing io_uring file descriptor operations...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Verify FD is valid */
    assert(ring.fd >= 0);

    /* Verify FD can be closed (will be closed again by cleanup) */
    int fd_copy = dup(ring.fd);
    assert(fd_copy >= 0);
    close(fd_copy);

    test_ring_cleanup(&ring);
    printf("  PASSED - FD operations work\n");
}

static void test_io_uring_mmap_access(void)
{
    printf("Testing io_uring mmap access...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Verify SQE array is accessible */
    assert(ring.sqes != NULL && ring.sqes != MAP_FAILED);

    /* Write to first SQE */
    ring.sqes[0].opcode = IORING_OP_NOP;
    ring.sqes[0].user_data = 0x12345678;

    /* Verify we can read it back */
    assert(ring.sqes[0].opcode == IORING_OP_NOP);
    assert(ring.sqes[0].user_data == 0x12345678);

    /* Verify ring metadata is accessible and initialized */
    assert(ring.sq_ring->ring_entries == 8);
    assert(ring.sq_ring->ring_mask == 7);
    assert(ring.cq_ring->ring_entries == 16);
    assert(ring.cq_ring->ring_mask == 15);

    test_ring_cleanup(&ring);
    printf("  PASSED - mmap access works\n");
}

static void test_io_uring_nop_via_ring(void)
{
    printf("Testing io_uring NOP via ring buffers...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Submit a NOP operation via ring buffers */
    unsigned int tail = ring.sq_ring->tail;
    unsigned int index = tail & ring.sq_ring->ring_mask;

    /* Fill in the SQE */
    struct io_uring_sqe *sqe = &ring.sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_NOP;
    sqe->user_data = 0xDEADBEEF;

    /* Update tail to submit */
    ring.sq_ring->tail = tail + 1;

    /* Call io_uring_enter to process */
    ret = sys_io_uring_enter(ring.fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
    assert(ret == 1); /* returns count of submitted SQEs, not a 0/1 success flag */

    /* Check completion */
    unsigned int cq_head = ring.cq_ring->head;
    unsigned int cq_tail = ring.cq_ring->tail;

    assert(cq_tail > cq_head);

    /* Read the completion */
    struct io_uring_cqe *cqe = &ring.cq_ring->cqes[cq_head & ring.cq_ring->ring_mask];
    assert(cqe->user_data == 0xDEADBEEF);
    assert(cqe->res == 0); /* NOP should succeed */

    /* Update head to consume */
    ring.cq_ring->head = cq_head + 1;

    test_ring_cleanup(&ring);
    printf("  PASSED - NOP via ring buffers works\n");
}

static void test_io_uring_file_io(void)
{
    printf("Testing io_uring file I/O...\n");

    struct test_ring ring;
    int ret = test_ring_init(&ring, 8);
    assert(ret == 0);

    /* Create a temporary file */
    const char *test_file = "/tmp/io_uring_test.txt";
    int test_fd = open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    assert(test_fd >= 0);

    /* Write some data via io_uring */
    const char *test_data = "Hello io_uring!";
    size_t test_len = strlen(test_data);

    unsigned int tail = ring.sq_ring->tail;
    unsigned int index = tail & ring.sq_ring->ring_mask;

    struct io_uring_sqe *sqe = &ring.sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_WRITE;
    sqe->fd = test_fd;
    sqe->addr = (uint64_t)test_data;
    sqe->len = test_len;
    sqe->off = 0;
    sqe->user_data = 0x1;

    ring.sq_ring->tail = tail + 1;

    ret = sys_io_uring_enter(ring.fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
    assert(ret == 1); /* returns count of submitted SQEs */

    /* Check write completion */
    unsigned int cq_head = ring.cq_ring->head;
    unsigned int cq_tail = ring.cq_ring->tail;
    assert(cq_tail > cq_head);

    struct io_uring_cqe *cqe = &ring.cq_ring->cqes[cq_head & ring.cq_ring->ring_mask];
    assert(cqe->user_data == 0x1);
    assert(cqe->res == (int32_t)test_len);

    ring.cq_ring->head = cq_head + 1;

    /* Read back via io_uring */
    char read_buf[128] = {0};
    tail = ring.sq_ring->tail;
    index = tail & ring.sq_ring->ring_mask;

    sqe = &ring.sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_READ;
    sqe->fd = test_fd;
    sqe->addr = (uint64_t)read_buf;
    sqe->len = test_len;
    sqe->off = 0;
    sqe->user_data = 0x2;

    ring.sq_ring->tail = tail + 1;

    ret = sys_io_uring_enter(ring.fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
    assert(ret == 1); /* returns count of submitted SQEs */

    /* Check read completion */
    cq_head = ring.cq_ring->head;
    cq_tail = ring.cq_ring->tail;
    assert(cq_tail > cq_head);

    cqe = &ring.cq_ring->cqes[cq_head & ring.cq_ring->ring_mask];
    assert(cqe->user_data == 0x2);
    assert(cqe->res == (int32_t)test_len);

    ring.cq_ring->head = cq_head + 1;

    /* Verify data */
    assert(memcmp(read_buf, test_data, test_len) == 0);

    close(test_fd);
    unlink(test_file);
    test_ring_cleanup(&ring);
    printf("  PASSED - file I/O via ring buffers works\n");
}

int main(int argc, char **argv)
{
    printf("===========================================\n");
    printf("OSv io_uring Test Suite\n");
    printf("===========================================\n\n");

    test_io_uring_setup();
    test_io_uring_init();
    test_io_uring_enter_basic();
    test_io_uring_register_buffers();
    test_io_uring_register_files();
    test_io_uring_multiple_rings();
    test_io_uring_invalid_params();
    test_io_uring_params_offsets();
    test_io_uring_fd_operations();

    printf("\nAdvanced tests (ring buffer access and I/O):\n");
    test_io_uring_mmap_access();
    test_io_uring_nop_via_ring();
    test_io_uring_enter_return_value();
    test_io_uring_file_io();
    test_io_uring_syscall_dispatch();

    printf("\nFeature/fidelity tests (Axis 1/2/3):\n");
    test_io_uring_setup_flags();
    test_io_uring_features();
    test_io_uring_disabled_ring();
    test_io_uring_iowq_max_workers();
    test_io_uring_ftruncate();
    test_io_uring_splice();
    test_io_uring_sync_file_range();
    test_io_uring_fixed_buffer_bounds();
    test_io_uring_msg_ring();
    test_io_uring_enter_ext_arg_timeout();

    printf("\n===========================================\n");
    printf("All io_uring tests PASSED!\n");
    printf("===========================================\n");
    return 0;
}
