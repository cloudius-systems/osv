#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char const *argv[])
{
	char path[64];

	strcpy(path, "/tmp/tst-truncateXXXXXX");
	int fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp");
		return 1;
	}

	if (write(fd, "123", 3) != 3) {
		perror("write");
		return 1;
	}

	close(fd);

	if (truncate64(path, 2)) {
		perror("truncate64");
		return 1;
	}

	char buf[3];
	fd = open(path, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	int count = read(fd, buf, sizeof(buf));
	if (count != 2) {
		perror("read");
		return 1;
	}

	close(fd);

	printf("Ok\n");
	return 0;
}
