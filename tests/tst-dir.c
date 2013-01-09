
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

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
		printf("name: %s, ino: %ld, off: %ld, reclen: %d, type %c\n",
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
		printf("name: %s, ino: %ld, off: %ld, reclen: %d, type %c\n",
			d->d_name, d->d_ino, d->d_off, d->d_reclen, d->d_type);
	} 

	if (closedir(dir) < 0) {
		perror("failed to close /");
		return EXIT_FAILURE;
	}

	return 0;
}
