
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE	4096

int main(int argc, char **argv)
{
	int fd;
        char *wbuf,*rbuf;

        // malloc is used since virt_to_phys doesn't work
        // on stack addresses and virtio needs that
        wbuf = malloc(BUF_SIZE);
        rbuf = malloc(BUF_SIZE);
	
        fd = open("/dev/vblk0", O_RDWR);
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
