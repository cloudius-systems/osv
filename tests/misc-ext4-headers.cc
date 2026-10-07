/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Mounts the ext4 image attached as the second disk (/dev/vblk1) and reads
// /f from it. scripts/tests/make_ext4_header_images.py builds one valid
// image and three with a single out-of-range header field each; boot once
// per image. The test needs that disk and an argument, so it is a misc-
// test, which scripts/test.py does not run on its own:
//
//   misc-ext4-headers.so good          mount and read succeed, contents match
//   misc-ext4-headers.so bad ERRNO     mount or read fails with ERRNO
//
// ERRNO is the error lwext4 gives for the field under test: ENOTSUP when
// the superblock check refuses the mount, EIO when an extent header is
// found corrupt. A different errno means the bad field was not detected
// and some later step failed by accident. For "bad" the program must also
// return normally: PASS is printed only if the guest is still running.

#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>

static const char expected[] = "lwext4 header test\n";

int main(int argc, char** argv)
{
    static const std::map<std::string, int> names = {
        {"EIO", EIO}, {"ENOTSUP", ENOTSUP}, {"EINVAL", EINVAL},
    };
    bool good = argc == 2 && !strcmp(argv[1], "good");
    bool bad = argc == 3 && !strcmp(argv[1], "bad") && names.count(argv[2]);
    if (!good && !bad) {
        printf("usage: misc-ext4-headers.so good | bad EIO|ENOTSUP|EINVAL\n");
        return 2;
    }
    int want = bad ? names.at(argv[2]) : 0;
    mkdir("/data", 0755);

    int err = 0;
    std::string got;
    if (mount("/dev/vblk1", "/data", "ext", 0, nullptr)) {
        err = errno;
        printf("mount: %s\n", strerror(err));
    } else {
        int fd = open("/data/f", O_RDONLY);
        if (fd < 0) {
            err = errno;
            printf("open: %s\n", strerror(err));
        } else {
            char buf[256];
            ssize_t n;
            while ((n = read(fd, buf, sizeof(buf))) > 0) {
                got.append(buf, n);
            }
            if (n < 0) {
                err = errno;
                printf("read: %s\n", strerror(err));
            }
            close(fd);
        }
        umount("/data");
    }

    bool ok;
    if (good) {
        ok = !err && got == expected;
    } else {
        ok = err == want;
    }
    printf("%s: misc-ext4-headers %s (errno %d, want %d, %zu bytes read)\n",
        ok ? "PASS" : "FAIL", argv[1], err, want, got.size());
    printf("SUMMARY: misc-ext4-headers %s %s\n", argv[1], ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
