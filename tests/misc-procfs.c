/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
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
#include <sys/types.h>
#include <dirent.h>

#define BUF_SIZE 4096

static void proc_readdir(const char *procdir)
{
	DIR *dirp;
	struct dirent *dp;

	if ((dirp = opendir(procdir)) == NULL) {
		perror("opendir");
		return;
	}
	printf("Reading directory entries at %s...\n", procdir);
	while ((dp = readdir(dirp)) != NULL) {
		printf("dentry name: %s\n", dp->d_name);
	}
	(void) closedir(dirp);
}

int main(int argc, char **argv)
{
	unsigned char buf[BUF_SIZE];
	int fd;

	fd = open("/proc/self/maps", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	for (;;) {
		size_t nr;
		int i;

		nr = read(fd, buf, BUF_SIZE);
		if (nr <= 0)
			break;

		for (i = 0; i < nr; i++) {
			putchar(buf[i]);
		}
	}
	if (close(fd) < 0) {
		perror("close");
	}

	proc_readdir("/proc");
	proc_readdir("/proc/self");

	return 0;
}
