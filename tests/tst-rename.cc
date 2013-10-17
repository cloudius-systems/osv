#define BOOST_TEST_MODULE __file__

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/test/unit_test.hpp>

namespace fs = boost::filesystem;

static const char *SECRET = "Hello, world";

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

static std::string read_line(const fs::path& path)
{
	std::string line;
	fs::ifstream file(path);
	std::getline(file, line);
	return line;
}

static void prepare_file(const fs::path& path)
{
	BOOST_REQUIRE_MESSAGE(!fs::exists(path), "File should not exist");

	BOOST_TEST_MESSAGE("Writing secret to " + path.string());
	fs::ofstream file(path);
	file << SECRET;
}

static void test_rename(const fs::path& src, const fs::path& dst)
{
	prepare_file(src);

	BOOST_TEST_MESSAGE("Renaming " + src.string() + " to " + dst.string());
	BOOST_REQUIRE_EQUAL(0, rename(src.c_str(), dst.c_str()));

	BOOST_TEST_MESSAGE("Checking content after rename");
	BOOST_CHECK_EQUAL(SECRET, read_line(dst));

	BOOST_CHECK_MESSAGE(!fs::exists(src), "Old file should not exist");
	BOOST_CHECK_MESSAGE(fs::remove(dst), "Sould be possible to remove new file");
}

BOOST_AUTO_TEST_CASE(test_renaming_in_the_same_directory)
{
	TempDir dir;

	test_rename(
		dir / "file1",
		dir / "file2");

	test_rename(
		dir / "a",
		dir / "aaaaa");

	test_rename(
		dir / "aaaaaaaaa",
		dir / "aa");
}
