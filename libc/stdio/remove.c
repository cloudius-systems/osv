#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include "syscall.h"

int remove(const char *path)
{
	int r = unlink(path);
	return (r && errno == EISDIR) ? rmdir(path) : r;
}
