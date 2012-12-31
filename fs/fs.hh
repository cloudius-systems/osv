#ifndef FS_HH
#define FS_HH

#include <string>
#include <cstdint>
#include <boost/intrusive_ptr.hpp>

class file;
class dir;

typedef boost::intrusive_ptr<file> fileref;
typedef boost::intrusive_ptr<dir> dirref;

class file {
public:
    file();
    virtual ~file();
    virtual uint64_t size() = 0;
    virtual void read(void *buffer, uint64_t offset, uint64_t len) = 0;
private:
    void ref();
    void unref();
private:
    unsigned _refs; // FIXME: make atomic
    friend void intrusive_ptr_add_ref(file* f) { f->ref(); }
    friend void intrusive_ptr_release(file* f) { f->unref(); }
};

class dir : public file {
public:
    virtual fileref open(std::string name) = 0;
    virtual dirref subdir(std::string name);
};

class filesystem {
public:
    virtual ~filesystem();
    virtual dirref root() = 0;
    fileref open(std::string name);
};

extern filesystem* rootfs;

#endif
