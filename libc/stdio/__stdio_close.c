#include <unistd.h>
#include "stdio_impl.h"

int __stdio_close(FILE *f)
{
	return close(f->fd);
}
