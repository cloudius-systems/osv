
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE	4096

int	 sys_mount(char *dev, char *dir, char *fsname, int flags, void *data);

int main(int argc, char **argv)
{
#define TESTDIR		"/mnt"
	char rbuf[BUF_SIZE];
	int fd;
	int ret;
	DIR *dir;
	char path[PATH_MAX];
	struct dirent *d;
	struct stat st;

	if (mkdir("/mnt", 0755) < 0) {
		printf("failed to create /mnt, error = %d\n", errno);
		return -1;
	}

	ret = sys_mount("/dev/vblk1", "/mnt", "fatfs", 0, NULL);
	if (ret) {
		printf("failed to mount fatfs, error = %d\n", ret);
		return ret;
	}

	dir = opendir(TESTDIR);
	if (!dir) {
		perror("failed to open testdir");
		return EXIT_FAILURE;
	}

	while ((d = readdir(dir))) {
		if (strcmp(d->d_name, ".") == 0 ||
		    strcmp(d->d_name, "..") == 0)
			continue;

		snprintf(path, PATH_MAX, "%s/%s", TESTDIR, d->d_name);
		if (stat(path, &st) < 0) {
			printf("failed to stat %s\n", path);
			continue;
		}

		if (!S_ISREG(st.st_mode)) {
			printf("ignoring %s, not a regular file\n", path);
			continue;
		}

		printf("found %s\n", d->d_name);
	}

	if (closedir(dir) < 0) {
		perror("failed to close testdir");
		return EXIT_FAILURE;
	}

	fd = open("/mnt/pthread.cc", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	memset(rbuf, 0, BUF_SIZE);
	if (pread(fd, rbuf, BUF_SIZE, 0) != BUF_SIZE) {
		perror("pread");
		return 1;
	}

	close(fd);
	return 0;
}
