#include "bootfs.hh"
#include <cstring>

extern char bootfs_start;

bootfs::bootfs()
    : _base(&bootfs_start)
    , _root(new dir(*this, "/"))
{
}

dirref bootfs::root()
{
    return _root;
}

fileref bootfs::do_open(std::string name)
{
    metadata *md = reinterpret_cast<metadata *>(_base);

    while (md->name[0]) {
	if (std::string(md->name) == name) {
	    return fileref(new file(*this, *md));
	} else if (std::string(md->name).find(name + "/") == 0) {
	    return fileref(new dir(*this, name + "/"));
	}
	++md;
    }
    return fileref();
}

bootfs::file::file(bootfs& fs, metadata& md)
    : _fs(fs), _md(md)
{
}

uint64_t bootfs::file::size()
{
    return _md.size;
}

void bootfs::file::read(void* buffer, uint64_t offset, uint64_t len)
{
    if (offset + len > _md.size) {
	throw -1; /* FIXME */
    }
    std::memcpy(buffer, _fs._base + _md.offset + offset, len);
}

bootfs::dir::dir(bootfs& fs, std::string path)
    : _fs(fs)
    , _path(path)
{
}

fileref bootfs::dir::open(std::string name)
{
    return _fs.do_open(_path + name);
}

uint64_t bootfs::dir::size()
{
    return 0;
}

void bootfs::dir::read(void* buffer, uint64_t offset, uint64_t len)
{
    throw -1; // FIXME
}
