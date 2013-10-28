/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _TST_FS_HH
#define _TST_FS_HH

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/filesystem/fstream.hpp>

#include "debug.hh"

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

const fs::path& mkfile(const fs::path& path)
{
	fs::ofstream fstream(path);
	return path;
}

void assert_exists(const fs::path path)
{
	struct stat buf;
	int error = stat(path.c_str(), &buf);
	BOOST_REQUIRE_MESSAGE(error == 0, fmt("Path %s should exist, errorno=%d") % path.string() % errno);
}

void assert_stat_error(const fs::path path, int expected_errno)
{
	struct stat buf;
	int error = stat(path.c_str(), &buf);
	BOOST_REQUIRE_MESSAGE(error == -1, fmt("Stat on %s should fail") % path.string());
	BOOST_REQUIRE_EQUAL(errno, expected_errno);
}

#endif
