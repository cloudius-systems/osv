
#include <unistd.h>
#include <libc/internal/libc.h>

ssize_t __read_chk(int fd, void *buf, size_t count, size_t bufsize)
{
    if (count > bufsize) {
        _chk_fail(__FUNCTION__);
    }
    return read(fd, buf, count);
}
