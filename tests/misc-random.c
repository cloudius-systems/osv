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
#include <assert.h>

#define BUF_SIZE 64

int main(int argc, char **argv)
{
	unsigned char rbuf[BUF_SIZE];
	int i, j;
	int fd;

	fd = open("/dev/random", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	memset(rbuf, 0, BUF_SIZE);

        for (i = 0; i < 10; i++) {
		size_t nr;

		nr = pread(fd, rbuf, BUF_SIZE, 0);

		assert(nr > 0);

		for (j = 0; j < nr; j++) {
			printf("%02x ", rbuf[j]);
		}
		printf("\n");
        }

	close(fd);

	return 0;
}
