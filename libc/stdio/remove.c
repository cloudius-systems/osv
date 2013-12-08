#include <stdio.h>
#include <errno.h>
#include <unistd.h>

int remove(const char *path)
{
	int r = unlink(path);
	// According to Posix, unlink() of a directory should return EPERM.
	// On Linux, it actually returns EISDIR. Let's check for both.
	return (r && (errno == EISDIR || errno == EPERM)) ? rmdir(path) : r;
}
