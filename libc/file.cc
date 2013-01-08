#define __FILE_defined
class FILE;

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <algorithm>
#include <dirent.h>
#include "libc.hh"

class FILE {
public:
    explicit FILE(int fd);
    ~FILE();
private:
    int _fd;
};

class __dirstream {
public:
    explicit __dirstream(int fd);
private:
    int _fd;
};

FILE::FILE(int fd)
    : _fd(fd)
{
}

__dirstream::__dirstream(int fd)
    : _fd(fd)
{
}

FILE* fopen(const char* fname, const char* fmode)
{
    static struct conv {
        const char* fmode;
        int mode;
    } modes[] = {
        { "r", O_RDONLY },
        { "r+", O_RDWR },
        { "w", O_WRONLY | O_CREAT | O_TRUNC },
        { "w+", O_RDWR | O_CREAT | O_TRUNC },
        { "a", O_WRONLY | O_APPEND | O_CREAT | O_TRUNC },
        { "a+", O_RDWR | O_APPEND | O_CREAT | O_TRUNC },
    };
    auto p = std::find_if(modes, modes + 6,
                 [=](conv c) { return strcmp(fmode, c.fmode) == 0; } );
    if (p == modes + 6) {
        return nullptr;
    }
    auto fd = ::open(fname, p->mode);
    if (fd == -1) {
        return nullptr;
    }
    return new FILE(fd);
}

DIR* opendir(const char* fname)
{
    auto fd = ::open(fname, O_RDONLY);
    if (fd == -1) {
        return libc_error_ptr<DIR>(ENOENT);
    }
    return new DIR(fd);
}
