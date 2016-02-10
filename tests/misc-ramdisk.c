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
	char wbuf[BUF_SIZE];
	char rbuf[BUF_SIZE];
	int fd;

	fd = open("/dev/ram0", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	memset(wbuf, 0xab, BUF_SIZE);
	if (pwrite(fd, wbuf, BUF_SIZE, 0) != BUF_SIZE) {
		perror("pwrite");
		return 1;
	}

	memset(rbuf, 0, BUF_SIZE);
	if (pread(fd, rbuf, BUF_SIZE, 0) != BUF_SIZE) {
		perror("pwrite");
		return 1;
	}
	if (memcmp(wbuf, wbuf, BUF_SIZE) != 0) {
		fprintf(stderr, "read error\n");
		return 1;
	}

	close(fd);
	return 0;
}
