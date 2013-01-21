#include <unistd.h>
#include "stdio_impl.h"

off_t __stdio_seek(FILE *f, off_t off, int whence)
{
	return lseek(f->fd, off, whence);
}
