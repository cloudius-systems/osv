#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "libc.h"

char *__randname(char *);

char *__mktemp(char *template)
{
	size_t l = strlen(template);
	int retries = 10000;

	if (l < 6 || memcmp(template+l-6, "XXXXXX", 6)) {
		errno = EINVAL;
		*template = 0;
		return template;
	}
	while (retries--) {
		__randname(template+l-6);
		if (access(template, F_OK) < 0) return template;
	}
	*template = 0;
	errno = EEXIST;
	return template;
}

weak_alias(__mktemp, mktemp);
