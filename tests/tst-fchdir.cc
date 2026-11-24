/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// fchdir() to a directory must make it the current directory, exactly like
// chdir() to its path: getcwd() must return its absolute path, and relative
// paths must be resolved in it. Check this for directories on the root
// filesystem, and for the root and a subdirectory of another mount.

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

// dir must contain a file named "f".
static void check_fchdir(const std::string& dir)
{
    int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    report(fd >= 0 && fchdir(fd) == 0, "fchdir() to " + dir);
    char cwd[PATH_MAX] = "";
    bool ok = getcwd(cwd, sizeof cwd) && dir == cwd;
    report(ok, "getcwd() after fchdir() to " + dir + " (got \"" + cwd + "\")");
    int f = open("f", O_RDONLY);
    report(f >= 0, "open(\"f\") after fchdir() to " + dir +
        (f < 0 ? std::string(": ") + strerror(errno) : ""));
    if (f >= 0) {
        close(f);
    }
    chdir("/");
    if (fd >= 0) {
        close(fd);
    }
}

static void create_file(const std::string& path)
{
    close(open(path.c_str(), O_CREAT | O_WRONLY, 0644));
}

int main()
{
    // On the root filesystem.
    const std::string dir = "/tmp/tst-fchdir";
    mkdir("/tmp", 0755);
    mkdir(dir.c_str(), 0755);
    create_file(dir + "/f");
    check_fchdir(dir);

    // The root and a subdirectory of another mount.
    const std::string mnt = dir + "/mnt";
    mkdir(mnt.c_str(), 0755);
    if (mount("", mnt.c_str(), "ramfs", 0, nullptr) == 0) {
        create_file(mnt + "/f");
        mkdir((mnt + "/sub").c_str(), 0755);
        create_file(mnt + "/sub/f");
        check_fchdir(mnt);
        check_fchdir(mnt + "/sub");
        umount(mnt.c_str());
    } else {
        report(false, "mount ramfs on " + mnt + ": " + strerror(errno));
    }
    rmdir(mnt.c_str());
    unlink((dir + "/f").c_str());
    rmdir(dir.c_str());

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
