
#include <unistd.h>
#include <libc/internal/libc.h>

extern "C" ssize_t __pread64_chk(int fd, void *buf, size_t count, off64_t offset, size_t bufsize)
{
    if (count > bufsize) {
        _chk_fail(__FUNCTION__);
    }
    return pread64(fd, buf, count, offset);
}
