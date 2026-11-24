/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// After a successful rename(), a file descriptor opened before the rename
// must keep working with the new name: fchdir() to it must make getcwd()
// return the new path, and openat() relative to it must find the directory's
// files. Both use the path stored in the file's dentry, so this checks that
// rename() stores the full new path there, and not just the new name.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <string>

static int tests, fails;

static void report(bool ok, const std::string& what)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

// Check that fd, a directory opened before it was renamed to new_path, still
// works: openat() finds its file "f", and fchdir() makes getcwd() return
// new_path (when check_cwd is set).
static void check_dir_fd(int fd, const std::string& new_path, bool check_cwd)
{
    int f = openat(fd, "f", O_RDONLY);
    report(f >= 0, "openat(fd, \"f\") after renaming the directory to " + new_path +
        (f < 0 ? std::string(": ") + strerror(errno) : ""));
    if (f >= 0) {
        close(f);
    }
    if (check_cwd) {
        char cwd[PATH_MAX] = "";
        bool ok = fchdir(fd) == 0 && getcwd(cwd, sizeof cwd) && new_path == cwd;
        report(ok, "getcwd() after fchdir(fd) is " + new_path + " (got \"" + cwd + "\")");
        chdir("/");
    }
}

static void make_dir_with_file(const std::string& dir)
{
    mkdir(dir.c_str(), 0755);
    close(open((dir + "/f").c_str(), O_CREAT | O_WRONLY, 0644));
}

static void remove_dir_with_file(const std::string& dir)
{
    unlink((dir + "/f").c_str());
    rmdir(dir.c_str());
}

// Rename a directory within its parent, and into another directory.
static void test_rename_dir(const std::string& base, bool check_cwd)
{
    const std::string a = base + "/a", b = base + "/b";
    const std::string d1 = base + "/d1", d2 = base + "/d2";
    remove_dir_with_file(a);
    remove_dir_with_file(b);
    remove_dir_with_file(d2 + "/y");
    remove_dir_with_file(d1 + "/x");
    rmdir(d1.c_str());
    rmdir(d2.c_str());

    make_dir_with_file(a);
    int fd = open(a.c_str(), O_RDONLY | O_DIRECTORY);
    report(fd >= 0 && rename(a.c_str(), b.c_str()) == 0, "rename " + a + " to " + b);
    check_dir_fd(fd, b, check_cwd);
    close(fd);
    remove_dir_with_file(b);

    mkdir(d1.c_str(), 0755);
    mkdir(d2.c_str(), 0755);
    make_dir_with_file(d1 + "/x");
    fd = open((d1 + "/x").c_str(), O_RDONLY | O_DIRECTORY);
    report(fd >= 0 && rename((d1 + "/x").c_str(), (d2 + "/y").c_str()) == 0,
        "rename " + d1 + "/x to " + d2 + "/y");
    check_dir_fd(fd, d2 + "/y", check_cwd);
    close(fd);
    remove_dir_with_file(d2 + "/y");
    rmdir(d1.c_str());
    rmdir(d2.c_str());
}

int main()
{
    // On the root filesystem.
    mkdir("/tmp", 0755);
    test_rename_dir("/tmp", true);

    // Renaming a directory in the root of a mount: the parent's own path is
    // "/" there. getcwd() is not checked there, because fchdir() does not
    // include the mount point in the cwd (a separate issue).
    const char* mnt = "/tmp/tst-rename-dentry-path-mnt";
    mkdir(mnt, 0755);
    if (mount("", mnt, "ramfs", 0, nullptr) == 0) {
        test_rename_dir(mnt, false);
        umount(mnt);
    } else {
        report(false, std::string("mount ramfs on ") + mnt + ": " + strerror(errno));
    }
    rmdir(mnt);

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
