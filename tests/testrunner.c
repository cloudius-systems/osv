
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void dlclose_by_path_np(const char* filename);

static unsigned nr_tests, nr_failures;

void load_test(char *path, char *argv0)
{
	void *handle;
	int (*test_main)(int argc, char **argv);
	int ret;

	printf("running %s\n", path);

	handle = dlopen(path, 0);
	test_main = dlsym(handle, "main");

	++nr_tests;
	ret = test_main(1, &argv0);
	if (ret) {
	    ++nr_failures;
		printf("failed.\n");
	} else {
		printf("ok.\n");
	}

	dlclose_by_path_np(path);
}

int osv_main(int argc, char **argv)
{
#define TESTDIR		"/tests"
	DIR *dir = opendir(TESTDIR);
	char path[PATH_MAX];
	struct dirent *d;
	struct stat st;

	if (!dir) {
		perror("failed to open testdir");
		return EXIT_FAILURE;
	}

	while ((d = readdir(dir))) {
		if (strcmp(d->d_name, ".") == 0 ||
		    strcmp(d->d_name, "..") == 0)
		continue;

		snprintf(path, PATH_MAX, "%s/%s", TESTDIR, d->d_name);
		if (stat(path, &st) < 0) {
			printf("failed to stat %s\n", path);
			continue;
		}

		if (!S_ISREG(st.st_mode)) {
			printf("ignoring %s, not a regular file\n", path);
			continue;
		}
		load_test(path, d->d_name);
	}

	if (closedir(dir) < 0) {
		perror("failed to close testdir");
		return EXIT_FAILURE;
	}
	printf("All tests complete - %d/%d failures\n", nr_failures, nr_tests);

	return 0;
}
