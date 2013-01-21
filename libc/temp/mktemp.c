#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include "libc.h"

char *__mktemp(char *template)
{
	struct timespec ts;
	size_t i, l = strlen(template);
	int retries = 10000;
	unsigned long r;

	if (l < 6 || strcmp(template+l-6, "XXXXXX")) {
		errno = EINVAL;
		*template = 0;
		return template;
	}
	while (retries--) {
		clock_gettime(CLOCK_REALTIME, &ts);
		r = ts.tv_nsec + (uintptr_t)&ts / 16 + (uintptr_t)template;
		for (i=1; i<=6; i++, r>>=4)
			template[l-i] = 'A'+(r&15);
		if (access(template, F_OK) < 0) return template;
	}
	*template = 0;
	errno = EEXIST;
	return template;
}

weak_alias(__mktemp, mktemp);
