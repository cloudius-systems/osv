#include <fcntl.h>
#include <vector>
#include <memory>
#include <stdint.h>
#include "fs/fs.hh"
#include "mutex.hh"
#include "libc.hh"
#include <assert.h>
#include <algorithm>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include "fs/stdio.hh"
#include "debug.hh"
#include <string.h>

class file_desc {
public:
    explicit file_desc(fileref file, bool canread, bool canwrite);
    fileref file() { return _file; }
    uint64_t pos() { return _pos; }
    void seek(uint64_t pos) { _pos = pos; }
private:
    fileref _file;
    uint64_t _pos;
    bool _canread;
    bool _canwrite;
};

struct file_table_type : std::vector<std::shared_ptr<file_desc>> {
    file_table_type();
};

mutex file_table_mutex;

file_table_type file_table;

file_table_type::file_table_type()
{
    std::shared_ptr<file_desc> con { new file_desc(console_fileref, true, true) };
    push_back(con); // 0
    push_back(con); // 1
    push_back(con); // 2
}

file_desc::file_desc(fileref f, bool canread, bool canwrite)
    : _file(f)
    , _pos()
    , _canread(canread)
    , _canwrite(canwrite)
{
}

int open(const char* fname, int mode, ...)
{
    assert(!(mode & O_APPEND));
    auto f = rootfs->open(fname);
    if (!f) {
        return libc_error(ENOENT);
    }
    auto desc = std::shared_ptr<file_desc>(
                    new file_desc(f, mode & O_RDONLY, mode & O_WRONLY));
    return with_lock(file_table_mutex, [&] {
        auto p = std::find(file_table.begin(), file_table.end(),
                           std::shared_ptr<file_desc>());
        if (p == file_table.end()) {
            file_table.push_back(desc);
            p = file_table.end() - 1;
        } else {
            *p = desc;
        }
        return p - file_table.begin();
    });
}

int open64(const char* fname, int mode, ...) __attribute__((alias("open")));

int close(int fd)
{
    if (fd < 0 || unsigned(fd) >= file_table.size()) {
        return libc_error(EBADF);
    }
    file_table[fd].reset();
    return 0;
}

std::shared_ptr<file_desc> get_fd(int fd)
{
    if (fd < 0) {
        return std::shared_ptr<file_desc>();
    }
    return with_lock(file_table_mutex, [=] {
        if (size_t(fd) >= file_table.size()) {
            return std::shared_ptr<file_desc>();
        }
        return file_table[fd];
    });
}

ssize_t write(int fd, const void* buffer, size_t len)
{
    auto desc = get_fd(fd);
    desc->file()->write(buffer, desc->pos(), len);
    desc->seek(desc->pos() + len);
    return 0;
}

ssize_t read(int fd, void *buf, size_t count)
{
    auto desc = get_fd(fd);
    auto size = desc->file()->size();
    if (desc->pos() >= size) {
        return 0;
    }
    count = std::min(count, size - desc->pos());
    desc->file()->read(buf, desc->pos(), count);
    desc->seek(desc->pos() + count);
    return count;
}

namespace {

int do_stat1(fileref f, struct stat* buf)
{
    if (!f) {
        return libc_error(ENOENT);
    }
    *buf = {};
    buf->st_size = f->size();
    // FIXME: could be a directory, but wait for hch's vfs work
    buf->st_mode = S_IFREG;
    // FIXME: stat missing fields
    return 0;
}

}

int __xstat(int ver, const char* path, struct stat* buf)
{
    assert(ver == 1);
    return do_stat1(rootfs->open(path), buf);
}

int mkdir(const char* path, mode_t mode)
{
    debug("mkdir not implemented");
    return libc_error(EROFS);
}

char* getcwd(char* path, size_t size)
{
    if (size < 2) {
        return libc_error_ptr<char>(ERANGE);
    }
    return strcpy(path, "/");
}
