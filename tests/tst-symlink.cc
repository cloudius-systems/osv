/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>

#define debug printf

#define TESTDIR	"/tmp"

#define N1	"f1"
#define N2	"f2"
#define N3	"f3"
#define D1	"d1"

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
	++tests;
	fails += !ok;
	debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
	if (fails)
	exit(0);
}

static void fill_buf(char *b, unsigned int no)
{
	memset(b, 'A', no - 1);
	b[no - 1] = 0;
}

static bool search_dir(const char *dir, const char *name)
{
	DIR		*d = opendir(dir);
	struct dirent	*e;

	report(d != NULL, "opendir");

	while ((e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, name) == 0) {
			return (true);
		}
	}

	return (false);
}

int main(int argc, char **argv)
{
	struct stat	buf;
	int		rc;
	int		error;
	time_t		t1;
	char		path[PATH_MAX];
	int		fd;
	int		fd1;
	char		b1[4097];
	char		b2[4097];

	debug("Testing symlink() and related functions.\n");

	report(sizeof(path) >= 4096, "sizeof(PATH_MAX)");

	report(chdir(TESTDIR) == 0, "chdir");

	/*
	 * test to check
	 *	access to symlink
	 *	file type
	 */
	fd = creat(N1, 0777);
	report(fd >= 0, "creat");
	report(search_dir(TESTDIR, N1) == true, "search dir");

	report(lstat(N1, &buf) == 0, "lstat");
	report(S_ISREG(buf.st_mode) == 1, "file mode");

	report(symlink(N1, N2) == 0, "symlink");
	report(search_dir(TESTDIR, N2) == true, "search dir");

	report(access(N1, R_OK | W_OK) == 0, "access");

	rc = access(N2, R_OK | W_OK);
	report(access(N2, R_OK | W_OK) == 0, "access");

	rc = readlink(N2, path, sizeof(path));
	report(rc >= 0, "readlink");
	path[rc] = 0;
	report(strcmp(path, N1) == 0, "readlink path");

	report(lstat(N2, &buf) == 0, "lstat");
	report(S_ISLNK(buf.st_mode) == 1, "file mode");

	close(fd);
	report(unlink(N1) == 0, "unlink");
	report(lstat(N2, &buf) == 0, "lstat");
	report(S_ISLNK(buf.st_mode) == 1, "file mode");

	rc	= stat(N2, &buf);
	error	= errno;
	report(rc < 0, "stat");
	report(error == ENOENT, "ENOENT expected");

	unlink(N1);
	unlink(N2);

	/*
	 * IO Tests 1: write(file), read(symlink), truncate(symlink)
	 */
	fd = creat(N1, 0777);
	report(fd >= 0, "creat");
	report(symlink(N1, N2) == 0, "symlink");

	fd1 = open(N2, O_RDONLY);
	report(fd1 >= 0, "symlink open");

	fill_buf(b1, sizeof(b1));

	rc = write(fd, b1, sizeof(b1));
	report(rc == sizeof(b1), "file write");
	fsync(fd);

	rc = read(fd1, b2, sizeof(b2));
	report(rc == sizeof(b2), "symlink read");

	report(memcmp(b1, b2, sizeof(b1)) == 0, "data verification");

#ifdef NOT_YET
	rc = ftruncate(fd1, 0);
	report(rc != 0 && errno == EINVAL, "symlink fd truncate");
#endif
	report(ftruncate(fd, 0) == 0, "file fd truncate");
	report(fstat(fd, &buf) == 0, "fstat file");
	report(buf.st_size == 0, "file size after truncate");

	close(fd);
	close(fd1);

	/*
	 * IO Tests 2: write(symlink), read(file)
	 */
	fd = open(N1, O_RDONLY);
	report(fd >= 0, "file open");

	fd1 = open(N2, O_WRONLY);
	report(fd1 >= 0, "symlink open");

	fill_buf(b1, sizeof(b1));

	rc = write(fd1, b1, sizeof(b1));
	report(rc == sizeof(b1), "file write");
	fsync(fd1);
	close(fd1);

	rc = read(fd, b2, sizeof(b2));
	report(rc == sizeof(b2), "symlink read");

	report(memcmp(b1, b2, sizeof(b1)) == 0, "data verification");

	/* truncate using symlink path */
	report(truncate(N2, 0) == 0, "symlink truncate");
	report(fstat(fd, &buf) == 0, "fstat file");
	report(buf.st_size == 0, "file size after truncate");

	close(fd);
	unlink(N2);
	unlink(N1);

	/*
	 * creating a symlink inside directory must change time
	 */
	fd = creat(N1, 0777);
	report(fd >= 0, "creat");
	report(mkdir(D1, 0777) == 0, "mkdir");
	report(stat(D1, &buf) == 0, "stat");
	t1 = buf.st_ctime;
	sleep(1);

	snprintf(path, sizeof(path), "%s/%s", D1, N2);
	report(symlink(N1, path) == 0, "symlink");
	report(stat(D1, &buf) == 0, "stat");

	report(t1 < buf.st_ctime, "ctime");
	report(t1 < buf.st_mtime, "mtime");

	close(fd);
	unlink(path);
	unlink(N1);
	rmdir(D1);

	/* ENOTDIR test */
	rc	= symlink(N1, path);
	error	= errno;
	report(rc < 0, "symlink");
	report(error == ENOTDIR || error == ENOENT, "ENOTDIR or ENOENT expected");

	/* name too long */
	fd = creat(N1, 0777);
	report(fd >= 0, "creat");

	fill_buf(path, 255);
	report(symlink(N1, path) == 0, "symlink");
	unlink(path);

	fill_buf(path, 257);
	rc      = symlink(N1, path);
	error   = errno;
	report(rc < 0, "symlink");
	report(error == ENAMETOOLONG, "ENAMETOOLONG expected 1");

	fill_buf(path, 4097);
	rc      = symlink(path, N1);
	error   = errno;
	report(rc < 0, "symlink");
	report(error == ENAMETOOLONG, "ENAMETOOLONG expected 2");

	rc      = symlink(N1, path);
	error   = errno;
	report(rc < 0, "symlink");
	report(error == ENAMETOOLONG, "ENAMETOOLONG expected 3");
	close(fd);
	unlink(N1);

	debug("SUMMARY: %d tests, %d failures\n", tests, fails);
	return (fails == 0 ? 0 : 1);
}
