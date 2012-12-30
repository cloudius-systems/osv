#ifndef FS_HH
#define FS_HH

#include <string>
#include <cstdint>

class file {
public:
    virtual ~file();
    virtual uint64_t size() = 0;
    virtual void read(void *buffer, uint64_t offset, uint64_t len) = 0;
};

class filesystem {
public:
    virtual ~filesystem();
    virtual file *open(std::string name) = 0;
};

extern filesystem* rootfs;

#endif
