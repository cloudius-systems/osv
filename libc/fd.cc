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


class file_desc {
public:
    explicit file_desc(fileref file, bool canread, bool canwrite);
private:
    fileref _file;
    uint64_t _pos;
    bool _canread;
    bool _canwrite;
};

mutex file_table_mutex;
std::vector<std::shared_ptr<file_desc>> file_table;

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
