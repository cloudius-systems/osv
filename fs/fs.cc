#include "fs.hh"
#include <fcntl.h>
#include <sys/stat.h>

file_::file_(int fd)
    : _fd(::dup(fd))
    , _refs(0)
{
}

file_::~file_()
{
    close(_fd);
}

void file_::ref()
{
    ++_refs;
}

void file_::unref()
{
    if (!--_refs) {
        delete this;
    }
}

uint64_t file_::size()
{
    struct stat st;

    ::__fxstat(1, _fd, &st);
    return st.st_size;
}

void file_::read(void *buffer, uint64_t offset, uint64_t len)
{
    ::lseek(_fd, offset, SEEK_SET);
    ::read(_fd, buffer, len);
}

void file_::write(const void* buffer, uint64_t offset, uint64_t len)
{
    ::lseek(_fd, offset, SEEK_SET);
    ::write(_fd, buffer, len);
}

filesystem::~filesystem()
{
}

fileref filesystem::open(std::string name)
{
    int fd = ::open(name.c_str(), O_RDONLY);
    if (fd < 0)
        return fileref();

    return fileref(new file_(fd));
}
