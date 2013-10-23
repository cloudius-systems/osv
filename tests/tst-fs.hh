/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _TST_FS_HH
#define _TST_FS_HH

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

class TempDir : public fs::path
{
public:
	TempDir() : fs::path(tmpnam(NULL)) {
		fs::create_directories(*this);
	}

	virtual ~TempDir() {
		fs::remove_all(*this);
	}
};

#endif
