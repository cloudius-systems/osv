/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// rename() between two directories of a ramfs must move the file or
// directory itself: its contents, attributes, inode number and open
// files must all stay with it.

#include <sys/mount.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>

static int tests = 0, fails = 0;

static void report(bool ok, const std::string& msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg.c_str());
    fflush(stdout);
}

static const std::string top = "/tmp/tst-ramfs-rename";

static bool write_file(const std::string& path, const char *data)
{
    int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = write(fd, data, strlen(data)) == (ssize_t)strlen(data);
    return close(fd) == 0 && ok;
}

static std::string read_file(const std::string& path)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return std::string("<open: ") + strerror(errno) + ">";
    }
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    return n < 0 ? "<read failed>" : std::string(buf, n);
}

static bool listed(const std::string& dir, const char *name)
{
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return false;
    }
    bool found = false;
    while (auto e = readdir(d)) {
        found |= !strcmp(e->d_name, name);
    }
    closedir(d);
    return found;
}

static bool absent(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) != 0 && errno == ENOENT;
}

static void test_move_file()
{
    const std::string a = top + "/a", b = top + "/b";
    report(write_file(a + "/f", "hello"), "create a/f");
    report(chmod((a + "/f").c_str(), 0640) == 0, "chmod a/f");
    struct stat st1, st2;
    stat((a + "/f").c_str(), &st1);
    int fd = open((a + "/f").c_str(), O_RDWR);
    report(fd >= 0, "open a/f");

    report(rename((a + "/f").c_str(), (b + "/f").c_str()) == 0, "rename a/f to b/f");

    // Check that the file left the source directory and is in the target one
    report(absent(a + "/f"), "a/f no longer exists");
    report(!listed(a, "f"), "a no longer lists f");
    report(listed(b, "f"), "b lists f");
    // Check that the file kept its contents, mode and inode number
    report(read_file(b + "/f") == "hello", "b/f has the contents of a/f");
    report(stat((b + "/f").c_str(), &st2) == 0 && st2.st_ino == st1.st_ino,
           "b/f has the inode number of a/f");
    report((st2.st_mode & 07777) == 0640, "b/f has the mode of a/f");
    // Check that the descriptor opened before the rename still reads and
    // writes the same file
    char buf[8] = {};
    report(pread(fd, buf, 5, 0) == 5 && !memcmp(buf, "hello", 5),
           "read through the descriptor opened before the rename");
    report(pwrite(fd, "J", 1, 0) == 1, "write through that descriptor");
    report(read_file(b + "/f") == "Jello", "b/f shows the write");
    close(fd);
    unlink((b + "/f").c_str());
}

static void test_move_directory()
{
    const std::string a = top + "/a", b = top + "/b";
    report(mkdir((a + "/d").c_str(), 0755) == 0, "mkdir a/d");
    report(write_file(a + "/d/x", "abc"), "create a/d/x");
    report(mkdir((a + "/d/sub").c_str(), 0755) == 0, "mkdir a/d/sub");
    report(write_file(a + "/d/sub/y", "xyz"), "create a/d/sub/y");

    report(rename((a + "/d").c_str(), (b + "/d").c_str()) == 0, "rename a/d to b/d");

    // Check that the directory took its whole subtree along
    report(absent(a + "/d"), "a/d no longer exists");
    report(read_file(b + "/d/x") == "abc", "b/d/x has the contents of a/d/x");
    report(read_file(b + "/d/sub/y") == "xyz", "b/d/sub/y has the contents of a/d/sub/y");
    report(listed(b + "/d", "x") && listed(b + "/d", "sub"), "b/d lists x and sub");
    // Check that the moved directory still works as one
    report(write_file(b + "/d/z", "new"), "create b/d/z");
    report(read_file(b + "/d/z") == "new", "read b/d/z");

    unlink((b + "/d/z").c_str());
    unlink((b + "/d/sub/y").c_str());
    rmdir((b + "/d/sub").c_str());
    unlink((b + "/d/x").c_str());
    report(rmdir((b + "/d").c_str()) == 0, "rmdir b/d once emptied");
}

static void test_replace_target()
{
    const std::string a = top + "/a", b = top + "/b";
    report(write_file(a + "/g", "new"), "create a/g");
    report(write_file(b + "/g", "old"), "create b/g");

    report(rename((a + "/g").c_str(), (b + "/g").c_str()) == 0, "rename a/g over b/g");

    // Check that exactly one g remains, with the contents of the source
    report(absent(a + "/g"), "a/g no longer exists");
    report(read_file(b + "/g") == "new", "b/g has the contents of a/g");
    int count = 0;
    DIR *d = opendir(b.c_str());
    while (auto e = d ? readdir(d) : nullptr) {
        count += !strcmp(e->d_name, "g");
    }
    if (d) {
        closedir(d);
    }
    report(count == 1, "b lists g once");
    unlink((b + "/g").c_str());
}

int main()
{
    if (mkdir(top.c_str(), 0755) != 0 && errno != EEXIST) {
        printf("FAIL: mkdir %s: %s\n", top.c_str(), strerror(errno));
        return 1;
    }
    if (mount("none", top.c_str(), "ramfs", 0, nullptr) != 0) {
        printf("FAIL: mount ramfs on %s: %s\n", top.c_str(), strerror(errno));
        return 1;
    }
    report(mkdir((top + "/a").c_str(), 0755) == 0, "mkdir a");
    report(mkdir((top + "/b").c_str(), 0755) == 0, "mkdir b");

    test_move_file();
    test_move_directory();
    test_replace_target();

    rmdir((top + "/a").c_str());
    rmdir((top + "/b").c_str());
    report(umount(top.c_str()) == 0, "unmount");
    rmdir(top.c_str());

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails == 0 ? 0 : 1;
}
