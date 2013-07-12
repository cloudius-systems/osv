
#include <sys/stat.h>
#include <sys/vfs.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#define BUF_SIZE	4096

int	 sys_mount(char *dev, char *dir, char *fsname, int flags, void *data);

int main(int argc, char **argv)
{
#define TESTDIR	"/usr"
//	char rbuf[BUF_SIZE];
	struct statfs st;
	DIR *dir;
	char path[PATH_MAX];
	struct dirent *d;
	struct stat s;
	int fd;

	if (statfs("/usr", &st) < 0)
		perror("statfs");

	printf("f_type: %ld\n", st.f_type); 
	printf("f_bsize: %ld\n", st.f_bsize); 
	printf("f_blocks: %ld\n", st.f_blocks); 
	printf("f_bfree: %ld\n", st.f_bfree); 
	printf("f_bavail: %ld\n", st.f_bavail); 
	printf("f_files: %ld\n", st.f_files); 
	printf("f_ffree: %ld\n", st.f_ffree); 
	printf("f_namelen: %ld\n", st.f_namelen); 

	dir = opendir(TESTDIR);
	if (!dir) {
		perror("failed to open testdir");
		return EXIT_FAILURE;
	}

	while ((d = readdir(dir))) {
		if (strcmp(d->d_name, ".") == 0 ||
		    strcmp(d->d_name, "..") == 0) {
		    	printf("found hidden entry %s\n", d->d_name);
			continue;
		}

		snprintf(path, PATH_MAX, "%s/%s", TESTDIR, d->d_name);
		if (stat(path, &s) < 0) {
			printf("failed to stat %s\n", path);
			continue;
		}

		if (!S_ISREG(s.st_mode) && !S_ISDIR(s.st_mode)) {
			printf("ignoring %s, not a regular file\n", path);
			continue;
		}

		printf("found %s\tsize: %ld\n", d->d_name, s.st_size);
	}

	if (closedir(dir) < 0) {
		perror("failed to close testdir");
		return EXIT_FAILURE;
	}

	fd = creat("/usr/foo", 0666);
	if (fd < 0) {
		perror("creat");
		return EXIT_FAILURE;
	}

	if (fstat(fd, &s) < 0) {
		perror("fstat");
		return EXIT_FAILURE;
	}
	printf("fstat done\n");

	close(fd);

#if 0
	fd = open("/mnt/tests/tst-zfs-simple.c", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	memset(rbuf, 0, BUF_SIZE);
	ret = pread(fd, rbuf, BUF_SIZE, 0);
	if (ret < 0) {
		perror("pread");
		return 1;
	}
	if (ret < BUF_SIZE) {
		fprintf(stderr, "short read\n");
		return 1;
	}

	close(fd);

//	rbuf[BUF_SIZE] = '\0';
//	printf("%s\n", rbuf);
#endif
	return 0;
}
