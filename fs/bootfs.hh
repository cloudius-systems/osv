#ifndef BOOTFS_HH
#define BOOTFS_HH

#include "fs.hh"

class bootfs : public filesystem {
public:
    bootfs();
    virtual fileref open(std::string name);
private:
    class file;
    struct metadata;
    char* _base;
    friend class file;
};

class bootfs::file : public ::file {
public:
    file(bootfs& fs, metadata& md);
    virtual uint64_t size();
    virtual void read(void* buffer, uint64_t offset, uint64_t len);
private:
    bootfs& _fs;
    metadata& _md;
};

struct bootfs::metadata {
    uint64_t size;
    uint64_t offset;
    char name[112];
};

#endif
