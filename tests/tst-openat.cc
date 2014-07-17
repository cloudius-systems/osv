#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define TESTDIR "/tmp"
#define N1      "f1"

static void report(bool ok, const char* msg)
{
    if (!ok) {
        printf("%s: FAIL (%s)\n", msg, strerror(errno));
        exit(0);
    } else {
        printf("%s: PASS\n", msg);
    }
}

int main()
{
    int fd;

    report(chdir(TESTDIR) == 0, "chdir");

    /* test 1: opening file in same directory */
    fd = creat(N1, 0777);
    report(fd >= 0, "creat");
    close(fd);

    DIR *d = opendir(TESTDIR);
    report(d != NULL, "opendir");

    fd = dirfd(d);
    report(fd >= 0, "dirfd");

    int fd1 = openat(fd, N1, O_RDONLY);
    report(fd1 >= 0, "openat");

    remove(N1);
    close(fd1);
    closedir(d);

    /* test 2: opening file across mount point */
    d = opendir("/dev/");
    report(d != NULL, "opendir");

    fd = dirfd(d);
    report(fd >= 0, "dirfd");

    fd1 = openat(fd, "random", O_RDONLY);
    report(fd1 >= 0, "openat");

    close(fd1);
    closedir(d);

    /* test 3: opening file with relative path */
    report(mkdir("/tmp/A", 0777) == 0, "mkdir");
    d = opendir("/tmp/A");
    report(d != NULL, "opendir");

    fd = dirfd(d);
    report(fd >= 0, "dirfd");

    fd1 = openat(fd, "1", O_RDONLY | O_CREAT, 0777);
    report(fd1 >= 0, "openat");

    close(fd1);
    closedir(d);

    /* test 4: opening file using AT_FDCWD */
    fd1 = openat(AT_FDCWD, "A/1", O_RDONLY);
    report(fd1 >= 0, "openat");
    close(fd1);

    /* test 5: opening file with invalid fd */
    fd1 = openat(-1, "/tmp/A/1", O_RDONLY);
    report(fd1 >= 0, "openat");
    close(fd1);

    remove("/tmp/A/1");
    remove("/tmp/A");
    return 0;
}
