/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Regression test for the ext_readlink()/ext_readdir() bounds hardening.
//
// ext-specific: it creates its own symlinks on a writable ext root, so build and
// run it with ext as the root filesystem:
//
//   scripts/build image=tests fs=ext
//   scripts/test.py
//
// The case that matters is the *slow* symlink. ext_readlink() serves any target
// shorter than sizeof(inode->blocks) (60 bytes) from the inline i_block fast
// path, which the hardening does not touch:
//
//   if (fsize < sizeof(inode_ref._ref.inode->blocks) && !blocks_count) {
//       return uiomove((char *)inode_ref._ref.inode->blocks, fsize, uio);
//   } else {
//       // malloc(block_size) + ext_internal_read(..., fsize, ...) <- guarded here
//   }
//
// So a target of 60 bytes or more is required to reach the guarded branch at
// all. An inline symlink additionally has i_blocks == 0, i.e. no data blocks,
// so it can never be served by the block path even in principle. A test using a
// short target therefore passes identically with and without the fix.
//
// Crafting the actual overflow (an inode whose i_size exceeds block_size) needs
// debugfs to build the image and cannot be done from inside the guest, so this
// test covers the legitimate side of the guards: every symlink length a guest
// can create must still round-trip exactly, including the longest one a single
// block can hold, which is the boundary the fsize <= block_size guard turns on.

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

#include <cassert>
#include <string>

// Create a symlink at @path pointing at @target, read it back, and require an
// exact round-trip.
static void check_symlink(const std::string& path, const std::string& target,
                          const char *what)
{
    unlink(path.c_str());
    if (symlink(target.c_str(), path.c_str()) != 0) {
        fprintf(stderr, "symlink(%s -> %zu bytes) FAILED: %s\n",
                path.c_str(), target.size(), strerror(errno));
        assert(false);
    }

    char buf[PATH_MAX];
    memset(buf, 0, sizeof(buf));
    ssize_t n = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (n < 0) {
        fprintf(stderr, "readlink(%s) FAILED: %s\n",
                path.c_str(), strerror(errno));
        assert(false);
    }
    buf[n] = 0;
    fprintf(stderr, "  %-28s target %4zu bytes, readlink returned %4zd\n",
            what, target.size(), n);
    assert(n == (ssize_t)target.size());
    assert(target == buf);

    unlink(path.c_str());
}

int main(int argc, char **argv)
{
    fprintf(stderr, "Running ext-readlink tests\n");

    // Default to the (writable) ext root; an optional argument points the same
    // binary at another directory, e.g. a separately mounted ext image.
    const std::string base = (argc > 1) ? argv[1] : "";

    // Fast path: target < 60 bytes, stored inline in the inode. Included so
    // both branches of ext_readlink() stay covered.
    check_symlink(base + "/tst-ext-link-inline", "/realfile", "inline (fast path)");

    // Slow path: >= 60 bytes, so the target is stored in a data block and read
    // back through the guarded malloc(block_size) + ext_internal_read() branch.
    check_symlink(base + "/tst-ext-link-slow", std::string(64, 'a'),
                  "slow, just over inline");

    // A longer slow target, still one block on any supported block size.
    check_symlink(base + "/tst-ext-link-slow2", "/" + std::string(299, 'b'),
                  "slow, 300 bytes");

    // The largest target a single 1 KiB block can hold: exactly the boundary the
    // fsize <= block_size guard tests, so this fails if the guard is off by one
    // or rejects a legitimate long symlink.
    check_symlink(base + "/tst-ext-link-max", "/" + std::string(1022, 'c'),
                  "slow, 1 KiB block max");

    // ext_readdir() name clamp: a legitimate 255-byte name (EXT4's maximum) must
    // still be returned intact, i.e. the clamp to sizeof(d_name)-1 = 255 does
    // not truncate any name a well-formed image can contain.
    {
        const std::string longest(255, 'd');
        const std::string dir = base + "/tst-ext-rd";
        const std::string path = dir + "/" + longest;
        rmdir(dir.c_str());
        assert(mkdir(dir.c_str(), 0755) == 0);
        int fd = creat(path.c_str(), 0644);
        assert(fd >= 0);
        close(fd);

        DIR *d = opendir(dir.c_str());
        assert(d != nullptr);
        bool found = false;
        struct dirent *de;
        while ((de = readdir(d)) != nullptr) {
            // Whatever the entry, the clamp must leave d_name NUL-terminated
            // inside its 256 bytes.
            assert(strlen(de->d_name) < sizeof(de->d_name));
            if (longest == de->d_name) {
                found = true;
            }
        }
        closedir(d);
        fprintf(stderr, "  %-28s 255-byte entry found: %s\n",
                "readdir max name", found ? "yes" : "no");
        assert(found);

        unlink(path.c_str());
        assert(rmdir(dir.c_str()) == 0);
    }

    fprintf(stderr, "ext-readlink tests PASSED\n");
    return 0;
}
