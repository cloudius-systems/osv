/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


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
#include <osv/mount.h>

#define BUF_SIZE	4096

int	 sys_mount(char *dev, char *dir, char *fsname, int flags, void *data);

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

#ifdef __OSV__
extern "C" int vfs_findroot(char *path, struct mount **mp, char **root);

int check_zfs_refcnt_behavior(void)
{
    struct mount *mp;
    char mount_path[] = "/";
    char file[64];
    int old_mcount;
    int fd, ret = 0;

    /* Get refcount value from the zfs mount point */
    ret = vfs_findroot(mount_path, &mp, (char **) &file);
    if (ret) {
        return -1;
    }
    old_mcount = mp->m_count;

    snprintf(file, 64, "/fileXXXXXX");
    mktemp(file);

    /* Create hard links, and remove them afterwards to exercise the refcount code */
    for (int i = 0; i < 10; i++) {
        fd = open(file, O_CREAT|O_TRUNC|O_WRONLY|O_SYNC, 0666);
        if (fd <= 0) {
            return -1;
        }
        close(fd);
        unlink(file);
    }

    /* Get the new refcount value after doing strategical fs operations */
    ret = vfs_findroot(mount_path, &mp, (char **) &file);
    if (ret) {
        return -1;
    }

    /* Must be equal */
    return !(old_mcount == mp->m_count);
}
#endif

int main(int argc, char **argv)
{
#define TESTDIR	"/usr"
//	char rbuf[BUF_SIZE];
	struct statfs st;
	DIR *dir;
	char path[PATH_MAX];
	struct dirent *d;
	struct stat s;
	char foo[PATH_MAX] = { 0 };
	int fd, ret;

	report(statfs("/usr", &st) == 0, "stat /usr");

	printf("f_type: %ld\n", st.f_type);
	printf("f_bsize: %ld\n", st.f_bsize);
	printf("f_blocks: %ld\n", st.f_blocks);
	printf("f_bfree: %ld\n", st.f_bfree);
	printf("f_bavail: %ld\n", st.f_bavail);
	printf("f_files: %ld\n", st.f_files);
	printf("f_ffree: %ld\n", st.f_ffree);
	printf("f_namelen: %ld\n", st.f_namelen);

	report((dir = opendir(TESTDIR)), "open testdir");

	while ((d = readdir(dir))) {
		if (strcmp(d->d_name, ".") == 0 ||
		    strcmp(d->d_name, "..") == 0) {
			printf("found hidden entry %s\n", d->d_name);
			continue;
		}

		snprintf(path, PATH_MAX, "%s/%s", TESTDIR, d->d_name);
		report((ret = stat(path, &s)) == 0, "stat file");
		if (ret < 0) {
			printf("failed to stat %s\n", path);
			continue;
		}

		report((ret = (S_ISREG(s.st_mode) || S_ISDIR(s.st_mode))),
			"entry must be a regular file");
		if (!ret) {
			printf("ignoring %s, not a regular file\n", path);
			continue;
		}

		printf("found %s\tsize: %ld\n", d->d_name, s.st_size);
	}

	report(closedir(dir) == 0, "close testdir");
	report(mkdir("/usr/testdir", 0777) == 0, "mkdir /usr/testdir (0777)");
	report(stat("/usr/testdir", &s) == 0, "stat /usr/testdir");

	fd = open("/usr/foo", O_CREAT|O_TRUNC|O_WRONLY|O_SYNC, 0666);
	report(fd > 0, "create /usr/foo");

	report(write(fd, &foo, sizeof(foo)) == sizeof(foo), "write sizeof(foo) bytes to fd");
	report(fsync(fd) == 0, "fsync fd");
	report(fstat(fd, &s) == 0, "fstat fd");

	printf("file size = %lld\n", s.st_size);

	report(close(fd) == 0, "close fd");

	fd = creat("/usr/foo", 0666);
	report(fd > 0, "possibly create /usr/foo again");

	report(fstat(fd, &s) == 0, "fstat fd");
	printf("file size = %lld (after O_TRUNC)\n", s.st_size);
	report(close(fd) == 0, "close fd again");

	report(rename("/usr/foo", "/usr/foo2") == 0,
		"rename /usr/foo to /usr/foo2");

	report(rename("/usr/foo2", "/usr/testdir/foo") == 0,
		"rename /usr/foo2 to /usr/testdir/foo");

	report(unlink("/usr/testdir/foo") == 0, "unlink /usr/testdir/foo");

	report(rename("/usr/testdir", "/usr/testdir2") == 0,
		"rename /usr/testdir to /usr/testdir2");

	report(rmdir("/usr/testdir2") == 0, "rmdir /usr/testdir2");
#ifdef __OSV__
	report(check_zfs_refcnt_behavior() == 0, "check zfs refcount consistency");
#endif

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

	// Report results.
	printf("SUMMARY: %d tests, %d failures\n", tests, fails);
	return 0;
}
