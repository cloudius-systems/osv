#ifndef BOOTFS_HH
#define BOOTFS_HH

#include "fs.hh"

class bootfs : public filesystem {
public:
    bootfs();
    virtual dirref root();
private:
    class file;
    class dir;
    struct metadata;
private:
    fileref do_open(std::string path);
private:
    char* _base;
    dirref _root;
    friend class file;
    friend class dir;
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

class bootfs::dir : public ::dir {
public:
    dir(bootfs& fs, std::string path);
    virtual fileref open(std::string name);
    virtual uint64_t size();
    virtual void read(void* buffer, uint64_t offset, uint64_t len);
private:
    bootfs& _fs;
    std::string _path;
};

struct bootfs::metadata {
    uint64_t size;
    uint64_t offset;
    char name[112];
};

#endif
