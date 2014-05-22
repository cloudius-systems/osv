/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

int main(void)
{
    char buf[32], ch;
    int i, ret;

    if (close(0) == -1) {
        perror("close fd 0");
        return -1;
    }

    ret = open("/dev/console", O_RDONLY);
    if (ret == -1) {
        perror("open /dev/console");
        return -1;
    } else if (ret != 0) {
        fprintf(stderr, "fd to /dev/console isn't 0\n");
        return -1;
    }

    if (!isatty(0)) {
        fprintf(stderr, "fd 0 isn't a tty\n");
        return -1;
    }

    for (i = 0; i < 32; i++) {
        ret = read(0, &ch, 1);
        if (ret == -1) {
            perror("read");
            return -1;
        } else if (ret == 0) {
            // handle EOF
            buf[i] = '\n';
            break;
        }

        buf[i] = ch;
        if (ch == '\n') {
            break;
        }
    }
    buf[i+1] = '\0';
    printf("%s", buf);
    return 0;
}
