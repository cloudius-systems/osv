#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include "libc.h"

char *__mktemp(char *);

int mkstemp(char *template)
{
	int fd, retries = 100, t0 = *template;
	while (retries--) {
		if (!*__mktemp(template)) return -1;
		if ((fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600))>=0)
			return fd;
		if (errno != EEXIST) return -1;
		/* this is safe because mktemp verified
		 * that we have a valid template string */
		template[0] = t0;
		strcpy(template+strlen(template)-6, "XXXXXX");
	}
	return -1;
}

LFS64(mkstemp);
