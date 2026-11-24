/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// libext can only mount one ext filesystem at a time: the state of a mounted
// ext filesystem is global. A second ext mount must therefore fail with
// EBUSY, and leave the first one working. This test runs with ext as the
// root filesystem (on the partition /dev/vblk0.1), and tries to mount the
// whole disk, /dev/vblk0, as ext as well. That mount used to fail only after
// overwriting the root filesystem's state with the whole disk's, so the root
// filesystem stopped working.
//
//   scripts/build image=tests fs=ext
//   scripts/test.py --name tst-ext-second-mount.so

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>

static int tests, fails;

static void report(bool ok, const char* what)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
}

int main()
{
    const char* mnt = "/tmp/tst-ext-second-mount";
    mkdir("/tmp", 0755);
    mkdir(mnt, 0755);

    int r = mount("/dev/vblk0", mnt, "ext", 0, nullptr);
    int err = errno;
    report(r == -1 && err == EBUSY, "a second ext mount fails with EBUSY");
    if (r == 0) {
        umount(mnt);
    }

    // The root filesystem must still work: read this test's own file.
    char buf[4096];
    int fd = open("/tests/tst-ext-second-mount.so", O_RDONLY);
    ssize_t n = fd >= 0 ? read(fd, buf, sizeof buf) : -1;
    report(n > 4 && !memcmp(buf, "\x7f" "ELF", 4), "the root filesystem still works");
    if (fd >= 0) {
        close(fd);
    }
    rmdir(mnt);

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
