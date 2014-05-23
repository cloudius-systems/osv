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

//#include <osv/debug.hh>

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

void fill_buf(char *b, size_t size, unsigned int no)
{
	memset(b, 'A', no - 1);
	b[no - 1] = 0;
}

bool search_dir(const char *dir, const char *name)
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

	debug("Testing symlink() and related functions.\n");

	report(sizeof(path) >= 4096, "sizeof(PATH_MAX)");

	report(chdir(TESTDIR) == 0, "chdir");

	/*
	 * test to check
	 *	access to symlink
	 *	file type
	 */
	report(creat(N1, 0777) >= 0, "creat");
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
	 * creating a symlink inside direct must change time
	 */
	report(mkdir(D1, 0777) == 0, "mkdir");
	report(stat(D1, &buf) == 0, "stat");
	t1 = buf.st_ctime;
	sleep(1);

	snprintf(path, sizeof(path), "%s/%s", D1, N2);
	report(symlink(N1, path) == 0, "symlink");
	report(stat(D1, &buf) == 0, "stat");

	report(t1 < buf.st_ctime, "ctime");
	report(t1 < buf.st_mtime, "mtime");

	unlink(path);
	rmdir(D1);

	/* ENOTDIR test */
	rc	= symlink(N1, path);
	error	= errno;
	report(rc < 0, "symlink");
	report(error == ENOTDIR || error == ENOENT, "ENOTDIR or ENOENT expected");

	/* name too long */
	report(creat(N1, 0777) >= 0, "creat");
	fill_buf(path, sizeof(path), 255);
	report(symlink(N1, path) == 0, "symlink");
	unlink(path);

	fill_buf(path, sizeof(path), 257);
	rc      = symlink(N1, path);
	error   = errno;
	report(rc < 0, "symlink");
	report(error == ENAMETOOLONG, "ENAMETOOLONG expected 1");
	unlink(N1);

	fill_buf(path, sizeof(path), 1024);
	report(symlink(path, N1) == 0, "symlink");
	unlink(N1);

	fill_buf(path, sizeof(path), 4097);
	rc      = symlink(path, N1);
	error   = errno;
	report(rc < 0, "symlink");
	report(error == ENAMETOOLONG, "ENAMETOOLONG expected 2");

	rc      = symlink(N1, path);
	error   = errno;
	report(rc < 0, "symlink");
	report(error == ENAMETOOLONG, "ENAMETOOLONG expected 3");


	debug("SUMMARY: %d tests, %d failures\n", tests, fails);
	return (fails == 0 ? 0 : 1);
}
