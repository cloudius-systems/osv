/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#ifndef OSV_TOOLS_MKFS_CPIO_HH_
#define OSV_TOOLS_MKFS_CPIO_HH_

#include <istream>
#include <string>

namespace osv {

class cpio_in {
public:
    virtual ~cpio_in();
    virtual void add_file(std::string name, std::istream& is) = 0;
public:
    static void parse(std::istream& is, cpio_in& out);
private:
    static bool parse_one(std::istream& is, cpio_in& out);
};

}

#endif /* OSV_TOOLS_MKFS_CPIO_HH_ */
