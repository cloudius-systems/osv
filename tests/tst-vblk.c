/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE	4096

int main(int argc, char **argv)
{
    int fd;
    char *wbuf,*rbuf,*origin;
    int i;
    int offset;

    // malloc is used since virt_to_phys doesn't work
    // on stack addresses and virtio needs that
    wbuf = malloc(BUF_SIZE);
    rbuf = malloc(BUF_SIZE);
    origin = malloc(BUF_SIZE); //preserves the prev content for next boot..

    fd = open("/dev/vblk0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    for (i=0;i<100; i++) {
        offset = i * 512;

        if (pread(fd, origin, BUF_SIZE, offset) != BUF_SIZE) {
            perror("pread, origin");
            return 1;
        }

        memset(wbuf, i, BUF_SIZE);
        if (pwrite(fd, wbuf, BUF_SIZE, offset) != BUF_SIZE) {
            perror("pwrite");
            return 1;
        }

        memset(rbuf, 0, BUF_SIZE);
        if (pread(fd, rbuf, BUF_SIZE, offset) != BUF_SIZE) {
            perror("pread");
            return 1;
        }

        if (memcmp(wbuf, rbuf, BUF_SIZE) != 0) {
            fprintf(stderr, "read error %i\n", i);
            return 1;
        }

        if (pwrite(fd, origin, BUF_SIZE, offset) != BUF_SIZE) {
            perror("pwrite origin");
            return 1;
        }

    }

    fprintf(stdout, "vblk test passed\n");

    close(fd);
    return 0;
}
