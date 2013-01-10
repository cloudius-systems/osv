
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int sys_mount(char *dev, char *dir, char *fsname, int flags, void *data);
int ll_readdir(int fd, struct dirent *d);

int test_vfs(void)
{
	struct dirent d;
	int fd, ret;
	const char test[] = "Hello World! HelloHello!";
	char buf[512];
	ssize_t len;

	fd = open("/test", O_RDWR|O_CREAT, 666);
	if (fd < 0) {
		printf("open returned %d", fd);
	}

	len = sizeof(test);
	if ((ret = write(fd, test, len)) != len) {
		printf("write failed, len %d out of %zd, errno %d\n",
			ret, len, errno);
	}

	if (lseek(fd, 0, SEEK_SET) != 0)
		printf("lseek returned %d", errno);

	if ((ret = read(fd, buf, sizeof(buf))) < len) {
		printf("read failed, len %d out of %ld, errno %d\n",
			ret, sizeof(buf), errno);
	}

	printf("payload: %s\n", buf);

	close(fd);
	
	fd = open("/", O_RDONLY, 0);
	if (fd < 0)
		printf("open root returned %d", fd);

	while (ll_readdir(fd, &d) == 0) {
		printf("name: %s, ino: %ld, off: %ld, reclen: %d, type %d\n",
			d.d_name, d.d_ino, d.d_off, d.d_reclen, d.d_type);
	}

	return ret;
}

int test_main(void)
{
	DIR *dir = opendir("/usr");
	struct dirent entry, *d;
	int ret;

	if (!dir) {
		perror("failed to open /usr");
		return EXIT_FAILURE;
	}

	printf("testing readdir:\n");
	while ((d = readdir(dir))) {
		printf("name: %s, ino: %ld, off: %ld, reclen: %d, type %d\n",
			d->d_name, d->d_ino, d->d_off, d->d_reclen, d->d_type);
	}

	if (!d && errno) {
		perror("readdir failed");
		return EXIT_FAILURE;
	}

	printf("testing readdir_r:\n");
	for (;;) {
		ret = readdir_r(dir, &entry, &d);
		if (ret) {
			errno = ret;
			perror("readdir_r failed");
			return EXIT_FAILURE;
		}
		if (!d)
			break;
		printf("name: %s, ino: %ld, off: %ld, reclen: %d, type %d\n",
			d->d_name, d->d_ino, d->d_off, d->d_reclen, d->d_type);
	} 

	if (closedir(dir) < 0) {
		perror("failed to close /");
		return EXIT_FAILURE;
	}

	test_vfs();
	return 0;
}
