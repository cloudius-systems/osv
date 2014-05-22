#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include "dirent.h"

DIR *fdopendir(int fd)
{
        DIR *dir;
        struct stat st;

        if (fstat(fd, &st) < 0) {
                return 0;
        }
        if (!S_ISDIR(st.st_mode)) {
                errno = ENOTDIR;
                return 0;
        }
        if (!(dir = malloc(sizeof *dir))) {
                return 0;
        }

        dir->fd = fd;
        return dir;
}
