/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Renaming a directory must leave its cached children safe to release.
//
// dentry_move() takes every cached child of the renamed directory off its
// hash chain. If a child is still referenced (an open file below the
// directory), its last release later unlinks it from the hash chain again.
// That second unlink is only safe if the first one left the child on a
// valid list; if it left the child's chain links pointing at its old
// neighbours, the release writes through pointers into dentries that may
// since have been unlinked, freed or reused, and corrupts the cache.
//
// Each round holds files open below a directory, surrounded on both sides
// by other held dentries so every hash chain has live neighbours, renames
// the directory, releases a share of those neighbours (so stale pointers
// now point at freed memory), then closes the children. A corrupted cache
// shows up as a crash, a wrong lookup result, or a name that resolves when
// it should not.
//
// Usage: tst-dentry-rename-child [rounds]   (default 200)

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static long bad;

static void fail(const char *what, const std::string &path, int err)
{
    if (bad++ < 20) {
        printf("FAIL: %s %s: %s\n", what, path.c_str(), strerror(err));
    }
}

static int open_new(const std::string &path)
{
    int fd = open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        fail("create", path, errno);
    }
    return fd;
}

static void close_all(std::vector<int> &fds)
{
    for (int fd : fds) {
        if (fd >= 0) {
            close(fd);
        }
    }
    fds.clear();
}

// Open n files named prefix0..prefix(n-1) in dir and keep them open.
static void hold(const std::string &dir, const char *prefix, int n,
                 std::vector<int> &fds)
{
    for (int i = 0; i < n; i++) {
        fds.push_back(open_new(dir + "/" + prefix + std::to_string(i)));
    }
}

static void unlink_all(const std::string &dir, const char *prefix, int n)
{
    for (int i = 0; i < n; i++) {
        unlink((dir + "/" + prefix + std::to_string(i)).c_str());
    }
}

static void round(const std::string &top, int r)
{
    const int nb = 64, nc = 16, na = 64;
    std::string a = top + "/a" + std::to_string(r);
    std::string b = top + "/b" + std::to_string(r);
    std::vector<int> before, children, after;

    // Neighbours inserted before the children: they end up behind them on
    // each chain (LIST_INSERT_HEAD), so they are the children's le_next.
    hold(top, "pre", nb, before);
    if (mkdir(a.c_str(), 0755) != 0) {
        fail("mkdir", a, errno);
        return;
    }
    hold(a, "c", nc, children);
    // Neighbours inserted after: they become the children's le_prev side.
    hold(top, "post", na, after);

    if (rename(a.c_str(), b.c_str()) != 0) {
        fail("rename", a + " -> " + b, errno);
    }

    // Release the neighbours on both sides. Their dentries are freed, so
    // any chain link still pointing at them is now dangling.
    close_all(before);
    unlink_all(top, "pre", nb);
    close_all(after);
    unlink_all(top, "post", na);

    // Allocate fresh dentries so freed memory gets reused.
    std::vector<int> churn;
    hold(top, "x", nb + na, churn);

    // Last references to the children: each drele() unlinks the child from
    // the hash chain it is on.
    close_all(children);

    // The cache must still be consistent.
    struct stat st;
    for (int i = 0; i < nc; i++) {
        std::string oldp = a + "/c" + std::to_string(i);
        std::string newp = b + "/c" + std::to_string(i);
        if (stat(oldp.c_str(), &st) == 0) {
            fail("stale name still resolves", oldp, EEXIST);
        }
        if (stat(newp.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            fail("renamed child", newp, errno ? errno : EINVAL);
        }
        unlink(newp.c_str());
    }
    for (int i = 0; i < nb + na; i++) {
        std::string p = top + "/x" + std::to_string(i);
        if (fstat(churn[i], &st) != 0 || !S_ISREG(st.st_mode) ||
            stat(p.c_str(), &st) != 0) {
            fail("neighbour", p, errno ? errno : EINVAL);
        }
    }
    close_all(churn);
    unlink_all(top, "x", nb + na);
    if (rmdir(b.c_str()) != 0) {
        fail("rmdir", b, errno);
    }
}

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? atoi(argv[1]) : 200;
    const char *top = "/tmp/dentry-rename-child";

    if (mkdir(top, 0755) != 0 && errno != EEXIST) {
        printf("FAIL: mkdir %s: %s\n", top, strerror(errno));
        return 1;
    }
    if (mount("none", top, "ramfs", 0, nullptr) != 0) {
        printf("FAIL: mount ramfs on %s: %s\n", top, strerror(errno));
        return 1;
    }
    for (int r = 0; r < rounds && bad == 0; r++) {
        round(top, r);
    }
    if (umount(top) != 0) {
        fail("umount", top, errno);
    }
    rmdir(top);

    if (bad) {
        printf("SUMMARY: %ld problem(s)\n", bad);
        return 1;
    }
    printf("PASS: %d rounds; renamed directories released their children cleanly\n",
           rounds);
    return 0;
}
