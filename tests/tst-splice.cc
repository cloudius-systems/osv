/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises splice(2), vmsplice(2), tee(2).  Built and run on the OSv test image.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

#include <cassert>
#include <climits>
#include <cstring>
#include <string>
#include <iostream>

int main()
{
    std::cerr << "Running splice tests\n";

    // write() all n bytes (regular files may in principle short-write).
    auto write_all = [](int fd, const char *p, size_t n) {
        size_t done = 0;
        while (done < n) {
            ssize_t w = write(fd, p + done, n - done);
            assert(w > 0);
            done += w;
        }
    };
    // read() all n bytes (pipes may return short reads).
    auto read_all = [](int fd, char *p, size_t n) {
        size_t done = 0;
        while (done < n) {
            ssize_t r = read(fd, p + done, n - done);
            assert(r > 0);
            done += r;
        }
    };

    // Prepare a source file with known contents.
    const char *src = "/tmp/splice-src";
    const char *dst = "/tmp/splice-dst";
    const size_t N = 40000;
    std::string data(N, 0);
    for (size_t i = 0; i < N; i++) {
        data[i] = (char)(i * 7 + 3);
    }
    int sfd = open(src, O_CREAT | O_TRUNC | O_RDWR, 0644);
    assert(sfd >= 0);
    write_all(sfd, data.data(), N);

    // splice file -> pipe -> file, and verify the bytes survive intact.
    int pfd[2];
    assert(pipe(pfd) == 0);
    int dfd = open(dst, O_CREAT | O_TRUNC | O_RDWR, 0644);
    assert(dfd >= 0);

    off_t in_off = 0;
    size_t moved = 0;
    while (moved < N) {
        // Move a bounded chunk file->pipe, then drain pipe->file, so the pipe
        // buffer never has to hold the whole file.
        ssize_t s = splice(sfd, &in_off, pfd[1], nullptr, 8192, 0);
        assert(s > 0);
        ssize_t drained = 0;
        while (drained < s) {
            ssize_t d = splice(pfd[0], nullptr, dfd, nullptr, s - drained, 0);
            assert(d > 0);
            drained += d;
        }
        moved += s;
    }
    assert(moved == N);

    // Verify dst == src.
    std::string check(N, 0);
    assert(pread(dfd, &check[0], N, 0) == (ssize_t)N);
    assert(check == data);

    // splice honors the offset pointer without moving the file position.
    assert(lseek(sfd, 0, SEEK_CUR) == N);  // our reads used the offset ptr, not the fd pos

    // Bad flags -> EINVAL.
    errno = 0;
    assert(splice(sfd, &in_off, pfd[1], nullptr, 10, 0x1000) == -1 && errno == EINVAL);

    // A non-null offset on a pipe (non-seekable) fd -> ESPIPE.
    errno = 0;
    off_t bad_off = 0;
    assert(splice(pfd[0], &bad_off, dfd, nullptr, 10, 0) == -1 && errno == ESPIPE);

    // An oversized length (> SSIZE_MAX) -> EINVAL (size_t wider than ssize_t).
    errno = 0;
    assert(splice(sfd, &in_off, dfd, nullptr, (size_t)SSIZE_MAX + 1, 0) == -1 && errno == EINVAL);

    // vmsplice: memory -> pipe, then read it back.
    const char *msg = "vmsplice payload";
    struct iovec iov { (void *)msg, strlen(msg) };
    ssize_t v = vmsplice(pfd[1], &iov, 1, 0);
    assert(v == (ssize_t)strlen(msg));
    char vbuf[64] = {};
    read_all(pfd[0], vbuf, strlen(msg));
    assert(memcmp(vbuf, msg, strlen(msg)) == 0);

    // tee is not supported (needs non-consuming pipe peek) -> ENOSYS, honestly.
    errno = 0;
    assert(tee(pfd[0], pfd[1], 10, 0) == -1 && errno == ENOSYS);

    close(pfd[0]); close(pfd[1]); close(sfd); close(dfd);
    unlink(src); unlink(dst);

    std::cerr << "splice tests PASSED\n";
    return 0;
}
