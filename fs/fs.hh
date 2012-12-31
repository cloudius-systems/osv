#ifndef FS_HH
#define FS_HH

#include <string>
#include <cstdint>
#include <boost/intrusive_ptr.hpp>

class file;

typedef boost::intrusive_ptr<file> fileref;

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

class filesystem {
public:
    virtual ~filesystem();
    virtual fileref open(std::string name) = 0;
};

extern filesystem* rootfs;

#endif
