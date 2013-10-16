#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

static const char *SECRET = "Hello, world";

static bool has_secret(const char *name) {
	FILE * file = fopen(name, "r");
	if (!file) {
		perror("Failed to open");
		return 1;
	}

	char buf[strlen(SECRET) + 1];
	fgets(buf, sizeof(buf), file);

	if (strcmp(SECRET, buf)) {
		fprintf(stderr, "File content does not match, "
			"expected %s but found %s, file=%s\n", SECRET, buf, file);
		fclose(file);
		return false;
	}

	fclose(file);
	return true;
}

static int do_rename(const char *src, const char *dst)
{
	printf("Renaming %s to %s\n", src, dst);

	if (rename(src, dst)) {
		perror("Failed to rename.");
		return 1;
	}

	if (!has_secret(dst)) {
		return 1;
	}

	return 0;
}

static int write_secret(const char *file_name) {
	printf("Writing secret to: %s\n", file_name);

	FILE * file = fopen(file_name, "w");
	if (!file) {
		perror("Failed to open");
		return 1;
	}

	fputs(SECRET, file);
	fclose(file);
	return 0;
}

int main(int argc, char const *argv[])
{
	const char * src_name = strdup(tmpnam(NULL));
	if (write_secret(src_name))
		return 1;

	const char * dst_name = strdup(tmpnam(NULL));
	if (do_rename(src_name, dst_name))
		return 1;

	const char *short_name = "a";
	if (do_rename(dst_name, short_name))
		return 1;

	if (do_rename(short_name, src_name))
		return 1;

	printf("OK.\n");
	return 0;
}
