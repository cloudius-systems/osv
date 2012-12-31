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

fileref filesystem::open(std::string name)
{
    dirref d = root();
    size_t s = 0, e;
    while (d && (e = name.find('/', s)) != name.npos) {
        if (s != e) {
            d = d->subdir(name.substr(s, e - s));
        }
        s = e + 1;
    }
    if (!d) {
        return fileref();
    }
    return d->open(name.substr(s, name.npos));
}

dirref dir::subdir(std::string name)
{
    // trivial implementation, can be overridden
    return boost::dynamic_pointer_cast<dir>(open(name));
}
