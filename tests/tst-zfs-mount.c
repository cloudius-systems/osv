
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
	struct statfs st;
	int ret;

	if (mkdir("/mnt", 0755) < 0) {
		printf("failed to create /mnt, error = %d\n", errno);
		return -1;
	}

	ret = sys_mount("/dev/vblk1", "/mnt", "zfs", 0, "osv/usr");
	if (ret) {
		printf("failed to mount zfs, error = %d\n", ret);
		return ret;
	}

	if (statfs("/mnt", &st) < 0)
		perror("statfs");

	printf("f_type: %ld\n", st.f_type); 
	printf("f_bsize: %ld\n", st.f_bsize); 
	printf("f_blocks: %ld\n", st.f_blocks); 
	printf("f_bfree: %ld\n", st.f_bfree); 
	printf("f_bavail: %ld\n", st.f_bavail); 
	printf("f_files: %ld\n", st.f_files); 
	printf("f_ffree: %ld\n", st.f_ffree); 
	printf("f_namelen: %ld\n", st.f_namelen); 

	return 0;
}
