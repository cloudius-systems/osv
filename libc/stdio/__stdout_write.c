#include "stdio_impl.h"
#include <termios.h>
#include <sys/ioctl.h>

extern int vfs_initialized;

size_t __stdout_write(FILE *f, const unsigned char *buf, size_t len)
{
	struct termios tio;

	if (!vfs_initialized)
		return 0;
		
	f->write = __stdio_write;
	if (!(f->flags & F_SVB) && ioctl(f->fd, TCGETS, &tio))
		f->lbf = -1;
	return __stdio_write(f, buf, len);
}
