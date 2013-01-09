#ifndef FS_HH
#define FS_HH

#include <dirent.h>
#include <string>
#include <cstdint>
#include <boost/intrusive_ptr.hpp>
#include <unordered_map>

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
    virtual void write(const void* buffer, uint64_t offset, uint64_t len) = 0;
    virtual int getdent(struct dirent *d, int idx);
private:
    void ref();
    void unref();
private:
    unsigned _refs; // FIXME: make atomic
    friend void intrusive_ptr_add_ref(file* f) { f->ref(); }
    friend void intrusive_ptr_release(file* f) { f->unref(); }
    friend class dir;
private:
    typedef std::pair<dirref, std::string> cache_key;
    // FIXME: an intrusive container
    typedef std::unordered_map<cache_key, fileref> cache_type;
    static cache_type _cache;
    friend struct std::hash<cache_key>;
};

class dir : public file {
public:
    fileref open(std::string name);
    virtual fileref do_open(std::string name) = 0;
    dirref subdir(std::string name);
    virtual void write(const void* buffer, uint64_t offset, uint64_t len);
private:
    dirref _parent;
    std::string _name;
};

class filesystem {
public:
    virtual ~filesystem();
    virtual dirref root() = 0;
    fileref open(std::string name);
};

extern filesystem* rootfs;

#endif
