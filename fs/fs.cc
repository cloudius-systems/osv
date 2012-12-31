#include "fs.hh"

filesystem* rootfs;

file::file()
    : _refs(0)
{
}

file::~file()
{
}

void file::ref()
{
    ++_refs;
}

void file::unref()
{
    if (!--_refs) {
        delete this;
    }
}

filesystem::~filesystem()
{
}
