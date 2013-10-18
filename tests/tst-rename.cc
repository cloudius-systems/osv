/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-rename

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/test/unit_test.hpp>

#include "tst-fs.hh"
#include "debug.hh"

namespace fs = boost::filesystem;

static const char *SECRET = "Hello, world";


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
	BOOST_REQUIRE_MESSAGE(file, "File should be open");
	file << SECRET;
}

static void check_file(const fs::path& path)
{
	BOOST_CHECK_EQUAL(SECRET, read_line(path));
}

template<typename T>
static bool contains(std::vector<T> values, T value)
{
	return std::find(values.begin(), values.end(), value) != values.end();
}

template<typename Iter>
static std::vector<std::string> to_strings(Iter values)
{
	std::vector<std::string> strings;
	for (auto v : values)
	{
		strings.push_back(std::to_string(v));
	}
	return strings;
}

static void assert_one_of(int value, std::vector<int> values)
{
	BOOST_REQUIRE_MESSAGE(contains(values, value),
		fmt("%s shound be in {%s}") % value % boost::algorithm::join(to_strings(values), ", "));
}

static void assert_rename_fails(const fs::path& src, const fs::path& dst, std::vector<int> errnos)
{
	BOOST_TEST_MESSAGE("Renaming " + src.string() + " to " + dst.string());
	BOOST_REQUIRE(rename(src.c_str(), dst.c_str()) == -1);
	assert_one_of(errno, errnos);
}

static void assert_renames(const fs::path src, const fs::path dst)
{
	BOOST_TEST_MESSAGE("Renaming " + src.string() + " to " + dst.string());
	int result = rename(src.c_str(), dst.c_str());
	BOOST_REQUIRE_MESSAGE(result == 0, fmt("Rename should succeed, errno=%d") % errno);
}

static void test_rename(const fs::path& src, const fs::path& dst)
{
	prepare_file(src);

	assert_renames(src, dst);

	check_file(dst);
	BOOST_CHECK_MESSAGE(!fs::exists(src), "Old file should not exist");
	BOOST_CHECK_MESSAGE(fs::remove(dst), "Sould be possible to remove new file");
}

static void test_file_rename_fails(const fs::path& src, const fs::path& dst, std::vector<int> errnos)
{
	prepare_file(src);
	assert_rename_fails(src, dst, errnos);
	check_file(src);
	BOOST_CHECK_MESSAGE(fs::remove(src), "Sould be possible to remove old file");
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

BOOST_AUTO_TEST_CASE(test_renaming_to_child_path_should_fail)
{
	TempDir dir;
	assert_rename_fails(dir, dir / "child", {EINVAL});
}

BOOST_AUTO_TEST_CASE(test_moving_file_to_another_directory)
{
	TempDir dir;

	std::string sub("sub");
	BOOST_REQUIRE(fs::create_directories(dir / sub));

	test_rename(
		dir / "file",
		dir / sub / "file");

	test_rename(
		dir / sub / "file2",
		dir / "file2");

	test_rename(
		dir / sub / "a",
		dir / "aaaa");
}

BOOST_AUTO_TEST_CASE(test_renaming_when_destination_is_substring)
{
	TempDir dir;

	test_rename(
		dir / "one_two",
		dir / "one");

	test_rename(
		dir / "two",
		dir / "two_one");
}

BOOST_AUTO_TEST_CASE(test_renaming_works_with_non_uniform_paths)
{
	TempDir dir;
	std::string sub("sub");
	fs::path file(dir / sub / "file");

	BOOST_REQUIRE(fs::create_directories(dir / sub));

	test_rename(file, dir / "/file2");
	test_rename(file, dir / "/sub///file2");
}

BOOST_AUTO_TEST_CASE(test_file_can_be_located_using_different_paths_after_rename)
{
	TempDir dir;
	fs::path file(dir / "file");
	fs::path subDir(dir / "sub");
	fs::path dst(subDir / "//file2");

	BOOST_REQUIRE(fs::create_directories(subDir));
	prepare_file(file);

	assert_renames(file, dst);

	check_file(dst);
	BOOST_CHECK(!fs::exists(file));
	BOOST_CHECK(fs::exists(dst));
	BOOST_CHECK(fs::exists(dir / "sub" / "file2"));
	BOOST_CHECK(fs::exists(dir / "sub" / ".." / "sub//file2"));
	BOOST_CHECK(fs::exists(dir / "sub" / "." / "file2"));
}

BOOST_AUTO_TEST_CASE(test_renaming_to_parent)
{
	TempDir dir;
	std::string sub("sub");
	fs::path file(dir / sub / "file");

	BOOST_REQUIRE(fs::create_directories(dir / sub));

	test_file_rename_fails(file, dir, {ENOTEMPTY, EISDIR, EEXIST});
	test_file_rename_fails(file, dir / sub, {ENOTEMPTY, EISDIR, EEXIST});
	test_file_rename_fails(file, dir / "/sub", {ENOTEMPTY, EISDIR, EEXIST});
	test_file_rename_fails(file, dir / "sub/", {ENOTDIR, EISDIR, EEXIST});
	test_file_rename_fails(file, dir / "/", {ENOTDIR, EEXIST, EISDIR});
	test_file_rename_fails(file, dir / "//", {ENOTDIR, EEXIST, EISDIR});
	test_file_rename_fails(file, dir / "///", {ENOTDIR, EEXIST, EISDIR});
	test_file_rename_fails(file, dir / "." / sub, {ENOTEMPTY, EEXIST, EISDIR});
	test_file_rename_fails(file, dir / sub / ".." / sub, {ENOTEMPTY, EEXIST, EISDIR});
	test_file_rename_fails(file, dir / sub / ".." / sub / "/", {ENOTDIR, EEXIST, EISDIR});
}

static fs::path make_dir(const fs::path& path)
{
	BOOST_REQUIRE(fs::create_directories(path));
	return path;
}

BOOST_AUTO_TEST_CASE(test_renaming_non_empty_directory)
{
	TempDir dir;

	fs::path src = dir / "sub1";
	fs::path dst = dir / "sub2";

	BOOST_REQUIRE(fs::create_directories(src));
	BOOST_REQUIRE(fs::exists(src));
	prepare_file(src / "file");

	assert_renames(src, dst);

	BOOST_CHECK(!fs::exists(src));
	BOOST_CHECK(!fs::exists(src / "file"));
	BOOST_CHECK(fs::exists(dst));
	check_file(dst / "file");

	BOOST_CHECK_MESSAGE(fs::remove_all(dst), "Sould be possible to remove new directory");
}

BOOST_AUTO_TEST_CASE(test_renaming_file_to_directory)
{
	TempDir tmp;
	fs::path file(tmp / "file");
	fs::path empty_dir = make_dir(tmp / "dir3");

	fs::path dir = make_dir(tmp / "dir");
	prepare_file(dir / "file2");

	test_file_rename_fails(file, dir, {EISDIR});
	test_file_rename_fails(file, empty_dir, {EISDIR});

	// See issue #68
	fs::remove_all(empty_dir);
}

BOOST_AUTO_TEST_CASE(test_renaming_directory_to_non_empty_directory)
{
	TempDir dir;
	fs::path src = make_dir(dir / "dir1");
	fs::path dst = make_dir(dir / "dir2");

	prepare_file(dst / "file2");
	assert_rename_fails(src, dst, {ENOTEMPTY, EEXIST});

	// See issue #68
	fs::remove_all(dst);
}

BOOST_AUTO_TEST_CASE(test_renaming_directory_to_empty_directory)
{
	TempDir dir;
	fs::path src = make_dir(dir / "dir1");
	fs::path dst = make_dir(dir / "dir2");

	assert_renames(src, dst);
	BOOST_REQUIRE(fs::exists(dst));
	BOOST_REQUIRE(!fs::exists(src));

	assert_renames(dst, src / "/");
	BOOST_REQUIRE(!fs::exists(dst));
	BOOST_REQUIRE(fs::exists(src));
}

BOOST_AUTO_TEST_CASE(test_renaming_file_to_non_existing_path_with_trailing_slash)
{
	TempDir dir;
	fs::path src(dir / "file");
	fs::path dst(dir / "dir2");

	BOOST_REQUIRE(!fs::exists(dst));
	prepare_file(src);

	assert_rename_fails(src, dst / "/", {ENOTDIR});
	assert_rename_fails(src, dst / "//", {ENOTDIR});
	assert_rename_fails(src, dst / "///", {ENOTDIR});

	assert_rename_fails(dst / "/", src, {ENOENT});
}

BOOST_AUTO_TEST_CASE(test_renaming_non_exising_file_should_fail)
{
	TempDir dir;
	fs::path dst(dir / "dst");

	assert_rename_fails(dir / "file", dst, {ENOENT});
	assert_rename_fails(dir / "file/", dst, {ENOENT});
}

BOOST_AUTO_TEST_CASE(test_renaming_exising_file_with_trailing_slash_to_non_existing_path_should_fail)
{
	TempDir dir;
	fs::path src(dir / "file");
	fs::path dst(dir / "file2");

	prepare_file(src);
	BOOST_REQUIRE(fs::exists(src));

	assert_rename_fails(src / "/", dst, {ENOTDIR});
	assert_rename_fails(src / "//", dst, {ENOTDIR});
	assert_rename_fails(src / "///", dst, {ENOTDIR});

	assert_rename_fails(src / "/", src / "/", {ENOTDIR});
}

BOOST_AUTO_TEST_CASE(test_renaming_directory_to_existing_file_when_dst_ends_with_slash_should_fail)
{
	TempDir dir;
	fs::path file(dir / "file");

	prepare_file(file);

	fs::path empty_dir = make_dir(dir / "empty_dir");
	assert_rename_fails(empty_dir, file / "/", {ENOTDIR});
	assert_rename_fails(empty_dir, file, {ENOTDIR});

	// See issue #68
	fs::remove_all(empty_dir);
	fs::remove_all(empty_dir);
}

BOOST_AUTO_TEST_CASE(test_renaming_directory_to_nonexisting_file_ending_with_slash_succeeds)
{
	TempDir tmp;
	fs::path dir = make_dir(tmp / "dir");

	assert_renames(dir, tmp / "dir2/");
}

BOOST_AUTO_TEST_CASE(test_renaming_root)
{
	TempDir dir;
	assert_rename_fails("/", dir, {EBUSY, EINVAL});
	assert_rename_fails(dir, "/", {EBUSY, EINVAL, EEXIST});
}

BOOST_AUTO_TEST_CASE(test_renaming_to_self_succeeds)
{
	TempDir dir;
	fs::path file(dir / "file");

	prepare_file(file);
	assert_renames(file, file);
	check_file(file);
}

BOOST_AUTO_TEST_CASE(test_renaming_dir_to_self_succeeds)
{
	TempDir dir;

	assert_renames(dir, dir);
	assert_renames(dir / "/", dir);
	assert_renames(dir / "/", dir / "/");
	assert_renames(dir, dir / "/");

	BOOST_REQUIRE(fs::exists(dir));
}

BOOST_AUTO_TEST_CASE(test_renaming_file_to_self_fails_if_path_ends_with_slash)
{
	TempDir dir;
	fs::path file(dir / "file");
	prepare_file(file);

	assert_rename_fails(file / "/", file / "/", {ENOTDIR});
	assert_rename_fails(file / "/", file, {ENOTDIR});
	assert_rename_fails(file, file / "/", {ENOTDIR});
}

BOOST_AUTO_TEST_CASE(test_renaming_with_last_coponent_as_dot_or_dot_dot_shall_fail)
{
	TempDir tmp;
	fs::path dir = make_dir(tmp / "dir");
	fs::path dir2 = make_dir(tmp / "sub" / "subsub");

	assert_rename_fails(dir, dir2 / "..",  {EBUSY, EINVAL});
	assert_rename_fails(dir, dir2 / "../", {EBUSY, EINVAL});
	assert_rename_fails(dir, dir2 / "..//", {EBUSY, EINVAL});
	assert_rename_fails(dir, dir2 / ".",   {EBUSY, EINVAL});
	assert_rename_fails(dir, dir2 / "./",  {EBUSY, EINVAL});
	assert_rename_fails(dir, dir2 / ".//",  {EBUSY, EINVAL});

	fs::path empty_dir = make_dir(tmp / "empty");
	assert_rename_fails(dir2 / "..",  empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(dir2 / "../", empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(dir2 / "..//", empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(dir2 / ".",   empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(dir2 / "./",  empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(dir2 / ".//",  empty_dir, {EBUSY, EINVAL});

	assert_rename_fails(fs::path("."), empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(fs::path("./"), empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(fs::path(".//"), empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(fs::path(".."), empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(fs::path("../"), empty_dir, {EBUSY, EINVAL});
	assert_rename_fails(fs::path("..//"), empty_dir, {EBUSY, EINVAL});

	// See issue #68.
	fs::remove_all(tmp / "sub");
	fs::remove_all(empty_dir);
}

BOOST_AUTO_TEST_CASE(test_renaming_with_empty_paths_fails)
{
	TempDir dir;
	assert_rename_fails("", dir, {ENOENT});
	assert_rename_fails(dir, "", {ENOENT});
}
