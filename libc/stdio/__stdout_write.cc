#include "stdio_impl.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <drivers/console.hh>

extern int vfs_initialized;

extern "C"
size_t __stdout_write(FILE *f, const unsigned char *buf, size_t len)
{
	struct termios tio;

	// We can only use __stdio_write, which uses file descriptor 1,
	// after vfs_init() opens this file descriptor. At that point, it
	// also sets vfs_initialized. Before that point, we can only use
	// use debug_write().
	if (!vfs_initialized) {
		console::write((const char *)f->wbase, f->wpos - f->wbase);
		f->wpos = f->wbase;
		console::write((const char *)buf, len);
		return len;
	}
		
	f->write = __stdio_write;
	if (!(f->flags & F_SVB) && ioctl(f->fd, TCGETS, &tio))
		f->lbf = -1;
	return __stdio_write(f, buf, len);
}
