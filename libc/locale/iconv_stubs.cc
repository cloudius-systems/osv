/*
 * Copyright (C) 2022 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <iconv.h>
#include <errno.h>
#include <osv/stubbing.hh>

iconv_t iconv_open(const char *to, const char *from) {
    WARN_STUBBED();
    errno = EINVAL;
    return (iconv_t)-1;
}

size_t iconv(iconv_t cd, char **in, size_t *inb, char **out, size_t *outb) {
    WARN_STUBBED();
    return 0l;
}

int iconv_close(iconv_t cd) {
    WARN_STUBBED();
    return 0l;
}
