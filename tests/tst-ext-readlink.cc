/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Regression test for the ext_readlink hardening: a normal symlink must still
// read back correctly (fast + slow), and the bounds guards must not break it.
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int main(int argc, char** argv)
{
    const char* link = (argc > 1) ? argv[1] : "/data/mylink";
    char buf[256];
    memset(buf, 0, sizeof(buf));
    ssize_t n = readlink(link, buf, sizeof(buf) - 1);
    if (n < 0) {
        fprintf(stderr, "readlink(%s) FAILED: %s\n", link, strerror(errno));
        return 1;
    }
    buf[n] = 0;
    fprintf(stderr, "readlink(%s) = '%s' (%zd bytes)\n", link, buf, n);
    if (strcmp(buf, "realfile") != 0) {
        fprintf(stderr, "READLINK MISMATCH: expected 'realfile'\n");
        return 1;
    }
    fprintf(stderr, "ext-readlink regression PASSED\n");
    return 0;
}
