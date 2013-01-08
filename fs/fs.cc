#include "fs.hh"

filesystem* rootfs;

namespace std {

template <>
struct hash<file::cache_key> {
    size_t operator()(file::cache_key k) const {
        return reinterpret_cast<uintptr_t>(k.first.get())
               ^ std::hash<std::string>()(k.second);
    }
};

}

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

fileref dir::open(std::string name)
{
    cache_key key(this, name);
    auto ret = _cache.find(key);
    if (ret == _cache.end()) {
        auto f = do_open(name);
        ret = _cache.insert(cache_type::value_type(key, f)).first;
    }
    return ret->second;
}

dirref dir::subdir(std::string name)
{
    // trivial implementation, can be overridden
    return boost::dynamic_pointer_cast<dir>(open(name));
}

void dir::write(const void* buffer, uint64_t offset, uint64_t len)
{
    abort();
}

file::cache_type file::_cache;
