/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <osv/run.hh>
#include <debug.hh>

#define TESTDIR		"/tests"

static unsigned nr_tests, nr_failures;

void load_test(char *path, char *argv0)
{
    printf("running %s\n", path);
    int ret;
    if (!osv::run(path, 1, &argv0, &ret)) {
        abort("Failed to execute or missing main()\n");
    }

	++nr_tests;
	if (ret) {
	    ++nr_failures;
		printf("failed.\n");
	} else {
		printf("ok.\n");
	}
}

int check_path(char *path)
{
	struct stat st;
	if (stat(path, &st) < 0) {
		printf("failed to stat %s\n", path);
		return 0;
	}

	if (!S_ISREG(st.st_mode)) {
		printf("ignoring %s, not a regular file\n", path);
		return 0;
	}
	return 1;
}

bool is_test_in_blacklist(const char *name, int argc, char **argv)
{
	/* Start from the index 2 as 1 would be -b */
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], name)) {
			return true;
		}
	}

	return false;
}

int main(int argc, char **argv)
{
	char path[PATH_MAX];
	bool blacklist = false;

	if (argc > 1 && !strcmp(argv[1], "-b")) {
		blacklist = true;
	}

	if (argc == 1 || blacklist) {
		DIR *dir = opendir(TESTDIR);
		struct dirent *d;

		if (!dir) {
			perror("failed to open testdir");
			return EXIT_FAILURE;
		}

		while ((d = readdir(dir))) {
			if (strcmp(d->d_name, ".") == 0 ||
			    strcmp(d->d_name, "..") == 0)
				continue;

			if (strncmp(d->d_name, "tst-", 4) != 0)
				continue;

			if (blacklist && is_test_in_blacklist(d->d_name, argc, argv))
				continue;

			snprintf(path, PATH_MAX, "%s/%s", TESTDIR, d->d_name);
			if (!check_path(path))
				continue;

			load_test(path, d->d_name);
		}

		if (closedir(dir) < 0) {
			perror("failed to close testdir");
			return EXIT_FAILURE;
		}
	} else {
		for (int i = 1; i < argc; i++) {
			snprintf(path, PATH_MAX, "%s/%s", TESTDIR, argv[i]);
			if (!check_path(path))
				continue;

			load_test(path, argv[i]);
		}
	}

	printf("All tests complete - %d/%d failures\n", nr_failures, nr_tests);

	return 0;
}
