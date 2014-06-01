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
#define N2	"f2_AAA"
#define N3	"f3"
#define N4	"f4"
#define N5	"f5"
#define D1	"d1"
#define D2	"d2_AAA"
#define D3	"d3"
#define D4	"d4"

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
			closedir(d);
			return (true);
		}
	}
	closedir(d);

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
	char		path1[PATH_MAX];

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

	rc	= unlink(N1);
	error	= errno;
	report(rc < 0 && errno == ENOENT, "ENOENT expected");
	report(unlink(N2) == 0, "unlink");

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
	report(unlink(N2) == 0, "unlink");
	report(unlink(N1) == 0, "unlink");

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
	report(unlink(path) == 0, "unlink");
	report(unlink(N1) == 0, "unlink");
	report(rmdir(D1) == 0, "rmdir");

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
	report(unlink(path) == 0, "unlink");

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
	report(unlink(N1) == 0, "unlink");

	/* O_NOFOLLOW test 1 */
	fd = creat(N1, 0777);
	report(fd >= 0, "creat");
	report(symlink(N1, N2) == 0, "symlink");

	rc = open(N2, O_RDONLY | O_NOFOLLOW);
	report(rc < 0 && errno == ELOOP, "open(symlink, O_NOFOLLOW) must fail");
	report(unlink(N2) == 0, "unlink");
	report(unlink(N1) == 0, "unlink");

	/* O_NOFOLLOW test 2 */
	report(mkdir(D1, 0777) == 0, "mkdir"); /* create dir /tmp/d1 */
	snprintf(path, sizeof(path), "%s/%s", D1, N1);
	fd = creat(path, 0777);  /* create file /tmp/d1/f1 */
	report(fd >= 0, "creat");
	close(fd);

	report(symlink(D1, N2) == 0, "symlink to directory");
	rc = open(N2, O_RDONLY | O_NOFOLLOW);
	report(rc < 0 && errno == ELOOP, "open(symlink, O_NOFOLLOW) must fail");

	rc = open(path, O_RDONLY | O_NOFOLLOW);
	report(rc >= 0, "open");
	close(rc);
	report(unlink(path) == 0, "unlink");
	report(unlink(N2) == 0, "unlink");
	report(rmdir(D1) == 0, "rmdir");

	/* unlink test */
	report(mkdir(D1, 0777) == 0, "mkdir"); /* create dir /tmp/d1 */
	snprintf(path, sizeof(path), "%s/%s", D1, N1);
	fd = creat(path, 0777);  /* create file /tmp/d1/f1 */
	report(fd >= 0, "creat");
	close(fd);

	report(symlink(D1, N2) == 0, "symlink to directory");
	report(search_dir(D1, N1) == true, "Directory search");
	report(search_dir(N2, N1) == true, "Symlink search");

	snprintf(path, sizeof(path), "%s/%s", N2, N1);
	report(unlink(path) == 0, "Unlink file through symlink");
	report(unlink(N2) == 0, "unlink");
	report(rmdir(D1) == 0, "rmdir");

	/* rename tests */
	fd = creat(N1, 0777);
	close(fd);
	report(rename(N1, N2) == 0, "rename file");
	report(unlink(N2) == 0, "unlink file");

	report(mkdir(D1, 0777) == 0, "mkdir");
	report(rename(D1, D2) == 0, "rename directory");
	report(rmdir(D2) == 0, "rmdir");

	report(mkdir(D1, 0777) == 0, "mkdir");		/* /tmp/d1 */
	snprintf(path, sizeof(path), "%s/%s", D1, N1);	/* /tmp/d1/f1 */
	fd = creat(path, 0777);
	report(fd >= 0, "create file");
	close(fd);

	report(symlink(D1, D2) == 0, "symlink to directory"); /* /tmp/d2 -> /tmp/d1 */
	snprintf(path1, sizeof(path1), "%s/%s", D1, N2);
	report(rename(path, path1) == 0, "rename(f1,f2)");

	snprintf(path, sizeof(path), "%s/%s", D1, N2);
	snprintf(path1, sizeof(path1), "%s/%s", D2, N3);
	report(rename(path, path1) == 0, "rename(d1/f2, d2/f3)");
	report(search_dir(D1, N3) == true, "Directory search");
	report(search_dir(D2, N3) == true, "Symlink search");

	snprintf(path, sizeof(path), "%s/%s", D2, N3);
	snprintf(path1, sizeof(path1), "%s/%s", D1, N4);
	report(rename(path, path1) == 0, "rename(d2/f3, d1/f4)");
	report(search_dir(D1, N4) == true, "Directory search");
	report(search_dir(D2, N4) == true, "Symlink search");

	snprintf(path, sizeof(path), "%s/%s", D2, N4);
	snprintf(path1, sizeof(path1), "%s/%s", D2, N5);
	report(rename(path, path1) == 0, "rename(d2/f4, d2/f5)");
	report(search_dir(D1, N5) == true, "Directory search");
	report(search_dir(D2, N5) == true, "Symlink search");

	report(rename(D2, D3) == 0, "rename(d2, d3)");
	rc = readlink(D3, path, sizeof(path));
	report(rc >= 0, "readlink");
	path[rc] = 0;
	report(strcmp(path, D1) == 0, "readlink path");

	report(rename(D1, D4) == 0, "rename(d1, d4)");
	rc = readlink(D3, path, sizeof(path));
	report(rc >= 0, "readlink");
	path[rc] = 0;
	report(strcmp(path, D1) == 0, "readlink path");
	report(lstat(D3, &buf) == 0, "lstat");
	report(S_ISLNK(buf.st_mode) == 1, "file mode");
	rc	= stat(D3, &buf);
	error	= errno;
	report(rc < 0, "stat");
	report(error == ENOENT, "ENOENT expected");

	snprintf(path, sizeof(path), "%s/%s", D4, N5);
	report(unlink(path) == 0, "unlink(d4/f5)");
	report(unlink(D3) == 0, "unlink(d3)");
	report(rmdir(D4) == 0, "rmdir");

	debug("SUMMARY: %d tests, %d failures\n", tests, fails);
	return (fails == 0 ? 0 : 1);
}
