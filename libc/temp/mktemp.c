#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
#include "libc.h"

char *__mktemp(char *template)
{
	struct timeval tv;
	size_t i, l = strlen(template);
	int retries = 10000;
	unsigned long r;

	if (l < 6 || strcmp(template+l-6, "XXXXXX")) {
		errno = EINVAL;
		*template = 0;
		return template;
	}
	while (retries--) {
		gettimeofday(&tv, NULL);
		r = tv.tv_usec + (uintptr_t)&tv / 16 + (uintptr_t)template;
		for (i=1; i<=6; i++, r>>=4)
			template[l-i] = 'A'+(r&15);
		if (access(template, F_OK) < 0) return template;
	}
	*template = 0;
	errno = EEXIST;
	return template;
}

weak_alias(__mktemp, mktemp);
