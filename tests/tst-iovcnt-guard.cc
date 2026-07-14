/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Regression: a huge iovcnt to readv/writev must return -1/EINVAL, not panic
// the kernel (previously an assert(niov <= UIO_MAXIOV) -> abort()).
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main()
{
    int fd = open("/tmp/iovcnt-test", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }
    // A normal small write to give the file content.
    struct iovec one = { (void*)"hi", 2 };
    if (writev(fd, &one, 1) != 2) { perror("writev(1)"); return 1; }
    lseek(fd, 0, SEEK_SET);

    // Oversized iovcnt: must be rejected with EINVAL, not abort the VM.
    struct iovec v = { 0, 0 };
    errno = 0;
    ssize_t r = readv(fd, &v, 0x40000000);
    if (!(r == -1 && errno == EINVAL)) {
        fprintf(stderr, "FAIL: readv(huge iovcnt) returned %zd errno=%d (want -1/EINVAL)\n", r, errno);
        return 1;
    }
    errno = 0;
    r = writev(fd, &v, 0x40000000);
    if (!(r == -1 && errno == EINVAL)) {
        fprintf(stderr, "FAIL: writev(huge iovcnt) returned %zd errno=%d (want -1/EINVAL)\n", r, errno);
        return 1;
    }
    close(fd);
    unlink("/tmp/iovcnt-test");
    fprintf(stderr, "iovcnt-guard test PASSED\n");
    return 0;
}
