#include "bootfs.hh"
#include <cstring>

extern char bootfs_start;

bootfs::bootfs()
    : _base(&bootfs_start)
{
}

file* bootfs::open(std::string name)
{
    metadata *md = reinterpret_cast<metadata *>(_base);

    while (md->name[0]) {
	if (std::string(md->name) == name) {
	    return new file(*this, *md);
	}
    }
    throw -1; /* FIXME */
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
