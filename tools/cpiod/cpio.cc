/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include "cpio.hh"
#include <istream>
#include <boost/lexical_cast.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/restrict.hpp>
#include <cstdlib>
#include <stdexcept>
#include <osv/align.hh>
#include <memory>
#include <sstream>
#include <cstring>
#include <cpio.h>
#include <iostream>
#include <sys/stat.h>

using namespace std;

namespace osv {

namespace io = boost::iostreams;

static const char* cpio_magic = "070701";

struct cpio_newc_header {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
};

uint32_t convert(char (&field)[8])
{
    char with_nul[9];
    std::copy(field, field + 8, with_nul);
    with_nul[8] = '\0';
    char* endptr;
    auto ret = strtoul(with_nul, &endptr, 16);
    if (endptr != with_nul + 8) {
        throw runtime_error("bad cpio format");
    }
    return ret;
}

cpio_in::~cpio_in()
{
}

bool cpio_in::parse_one(istream& is, cpio_in& out)
{
    cpio_newc_header header;
    is.read((char*)&header, sizeof(header));
    if (strncmp(cpio_magic, header.c_magic, 6) != 0) {
        throw runtime_error(string("bad cpio magic: '") + string(header.c_magic, 6) + "'");
    }
    auto namesize = convert(header.c_namesize);
    auto aligned = align_up(sizeof(header) + namesize, sizeof(uint32_t)) - sizeof(header);
    unique_ptr<char[]> namebuf{new char[aligned]};
    is.read(namebuf.get(), aligned);
    string name{namebuf.get(), namesize - 1};
    if (name == "TRAILER!!!") {
        return false;
    }
    auto mode = convert(header.c_mode);
    auto filesize = convert(header.c_filesize);
    auto aligned_filesize = align_up(filesize, 4u);

    auto type = mode & 0170000;
    auto perm = mode & 0777;

    switch (type) {
    case C_ISREG: {
        auto file_slice = io::restrict(is, 0, filesize);
        io::stream<decltype(file_slice)> file(file_slice);
        file.rdbuf()->pubsetbuf(nullptr, 0);

        out.add_file(name, file, perm);
        break;
    }
    case C_ISDIR: {
        if (filesize > 0) {
            throw runtime_error("bad directory size");
        }
        out.add_dir(name, perm);
        break;
    }
    case C_ISLNK: {
        unique_ptr<char[]> targetbuf{new char[filesize]};
        is.read(targetbuf.get(), filesize);
        string target{targetbuf.get(), filesize};
        out.add_symlink(target, name, perm);
	break;
    }
    default:
        cout << name << ": unknown type " << type << "\n";
        is.ignore(filesize);
        break;
    }

    is.ignore(aligned_filesize - filesize);
    return true;
}

void cpio_in::parse(istream& is, cpio_in& out)
{
    is.rdbuf()->pubsetbuf(nullptr, 0);
    while (parse_one(is, out))
        ;
}

}
