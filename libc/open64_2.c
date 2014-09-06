#include <fcntl.h>
#include <errno.h>

int __open64_2(const char *file, int flags)
{
    if (flags & O_CREAT) {
        errno = EINVAL;
        return -1;
    }

    return open64(file, flags);
}
