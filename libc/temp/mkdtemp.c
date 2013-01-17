#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include "libc.h"

char *__mktemp(char *);

char *mkdtemp(char *template)
{
	int retries = 100, t0 = *template;
	while (retries--) {
		if (!*__mktemp(template)) return 0;
		if (!mkdir(template, 0700)) return template;
		if (errno != EEXIST) return 0;
		/* this is safe because mktemp verified
		 * that we have a valid template string */
		template[0] = t0;
		strcpy(template+strlen(template)-6, "XXXXXX");
	}
	return 0;
}
