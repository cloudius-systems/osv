#ifndef FS_HH
#define FS_HH

#include <dirent.h>
#include <string>
#include <cstdint>
#include <boost/intrusive_ptr.hpp>
#include <unordered_map>

class file_;

typedef boost::intrusive_ptr<file_> fileref;

class file_ {
public:
    file_(int fd);
    ~file_();
    uint64_t size();
    void read(void *buffer, uint64_t offset, uint64_t len);
    void write(const void* buffer, uint64_t offset, uint64_t len);
private:
    void ref();
    void unref();
    int _fd;
private:
    unsigned _refs; // FIXME: make atomic
    friend void intrusive_ptr_add_ref(file_* f) { f->ref(); }
    friend void intrusive_ptr_release(file_* f) { f->unref(); }
};

class filesystem {
public:
    virtual ~filesystem();
    fileref open(std::string name);
};

#endif
