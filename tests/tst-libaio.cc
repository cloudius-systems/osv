/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises the Linux libaio interface (io_setup/io_submit/io_getevents/
// io_destroy/io_cancel) implemented in core/libaio.cc.  This test targets
// OSv's <libaio.h> ABI (flat struct iocb with aio_* fields); it is built and
// run as part of the OSv test image.

#include <libaio.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include <cassert>
#include <cstdint>
#include <iostream>

// Helpers matching the fields libaio's io_prep_* would set.
static void prep_pwrite(struct iocb *cb, int fd, void *buf, size_t n, long long off)
{
    memset(cb, 0, sizeof(*cb));
    cb->aio_lio_opcode = IO_CMD_PWRITE;
    cb->aio_fildes = fd;
    cb->aio_buf = reinterpret_cast<uint64_t>(buf);
    cb->aio_nbytes = n;
    cb->aio_offset = off;
}

static void prep_pread(struct iocb *cb, int fd, void *buf, size_t n, long long off)
{
    memset(cb, 0, sizeof(*cb));
    cb->aio_lio_opcode = IO_CMD_PREAD;
    cb->aio_fildes = fd;
    cb->aio_buf = reinterpret_cast<uint64_t>(buf);
    cb->aio_nbytes = n;
    cb->aio_offset = off;
}

// Collect exactly n events, tolerating short io_getevents returns.
static void get_n_events(io_context_t ctx, long n, struct io_event *evs)
{
    long got = 0;
    while (got < n) {
        int r = io_getevents(ctx, 1, n - got, evs + got, nullptr);
        assert(r > 0);
        got += r;
    }
}

// A write followed by a read back should return the same bytes.
static void test_write_read_roundtrip()
{
    char path[] = "/tmp/tst-libaio-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);

    const size_t N = 8192;
    char *wbuf = static_cast<char *>(malloc(N));
    char *rbuf = static_cast<char *>(malloc(N));
    for (size_t i = 0; i < N; i++) {
        wbuf[i] = static_cast<char>((i * 13 + 5) & 0xff);
    }
    memset(rbuf, 0, N);

    io_context_t ctx = nullptr;
    assert(io_setup(8, &ctx) == 0);

    // Submit the write.
    struct iocb cbw;
    prep_pwrite(&cbw, fd, wbuf, N, 0);
    cbw.aio_data = 0x1111;
    struct iocb *pw = &cbw;
    assert(io_submit(ctx, 1, &pw) == 1);

    struct io_event ev;
    get_n_events(ctx, 1, &ev);
    assert(ev.res == (long)N);        // all bytes written
    assert(ev.data == 0x1111);        // aio_data echoed back
    assert(ev.obj == reinterpret_cast<uint64_t>(&cbw));

    // Now read it back.
    struct iocb cbr;
    prep_pread(&cbr, fd, rbuf, N, 0);
    cbr.aio_data = 0x2222;
    struct iocb *pr = &cbr;
    assert(io_submit(ctx, 1, &pr) == 1);
    get_n_events(ctx, 1, &ev);
    assert(ev.res == (long)N);
    assert(ev.data == 0x2222);
    assert(memcmp(wbuf, rbuf, N) == 0);

    assert(io_destroy(ctx) == 0);
    free(wbuf);
    free(rbuf);
    close(fd);
    unlink(path);
}

// Submit several ops at once and collect all their completions.
static void test_batch()
{
    char path[] = "/tmp/tst-libaio-batch-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);

    io_context_t ctx = nullptr;
    assert(io_setup(16, &ctx) == 0);

    const int B = 8;
    const size_t N = 4096;
    char *bufs[B];
    struct iocb cbs[B];
    struct iocb *ps[B];
    for (int i = 0; i < B; i++) {
        bufs[i] = static_cast<char *>(malloc(N));
        memset(bufs[i], 'A' + i, N);
        prep_pwrite(&cbs[i], fd, bufs[i], N, (long long)i * N);
        cbs[i].aio_data = 0x100 + i;
        ps[i] = &cbs[i];
    }
    assert(io_submit(ctx, B, ps) == B);

    struct io_event evs[B];
    get_n_events(ctx, B, evs);
    long total = 0;
    for (int i = 0; i < B; i++) {
        assert(evs[i].res == (long)N);
        total += evs[i].res;
    }
    assert(total == (long)(B * N));

    assert(io_destroy(ctx) == 0);
    for (int i = 0; i < B; i++) {
        free(bufs[i]);
    }
    close(fd);
    unlink(path);
}

// A bad fd must surface as -EBADF in the event's res, not a crash.
static void test_bad_fd()
{
    io_context_t ctx = nullptr;
    assert(io_setup(4, &ctx) == 0);

    char buf[512];
    struct iocb cb;
    prep_pread(&cb, 987654, buf, sizeof(buf), 0);  // fd not open
    struct iocb *p = &cb;
    assert(io_submit(ctx, 1, &p) == 1);

    struct io_event ev;
    get_n_events(ctx, 1, &ev);
    assert(ev.res == -EBADF);

    assert(io_destroy(ctx) == 0);
}

// io_getevents with a timeout must return 0 when nothing is pending.
static void test_getevents_timeout()
{
    io_context_t ctx = nullptr;
    assert(io_setup(4, &ctx) == 0);

    struct io_event ev;
    struct timespec ts { 0, 50 * 1000 * 1000 };  // 50 ms
    int r = io_getevents(ctx, 1, 1, &ev, &ts);
    assert(r == 0);  // timed out with no events

    assert(io_destroy(ctx) == 0);
}

int main()
{
    std::cerr << "Running libaio tests\n";

    test_write_read_roundtrip();
    test_batch();
    test_bad_fd();
    test_getevents_timeout();

    std::cerr << "libaio tests PASSED\n";
    return 0;
}
