/*
 * Copyright (C) 2018 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define RANDOM_PATH "/dev/random"
//
// This implementation is based on the specification described at http://man7.org/linux/man-pages/man2/getrandom.2.html
// with following limitations:
//
// - given /dev/random and /dev/urandom provide same behavior in OSv (backed by the same code per drivers/random.cc),
//   getrandom does not read flags argument to differentiate quality of random data returned
// - does not check if random source data is available
// - does not differentiate between blocking vs non-blocking behavior
ssize_t getrandom(void *buf, size_t count, unsigned int flags)
{
    int fd;
    ssize_t read;

    fd = open(RANDOM_PATH, O_RDONLY);
    if (fd < 0) {
        errno = EAGAIN;
        return -1;
    }

    memset(buf, 0, count);
    read = pread(fd, buf, count, 0);

    if(read <= 0) {
        errno = EAGAIN;
        close(fd);
        return -1;
    }

    close(fd);
    return read;
}
