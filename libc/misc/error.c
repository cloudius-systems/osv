/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

extern char *program_invocation_name;

void
error (int status, int errnum, const char *message, ...)
{
    fflush(stdout);

    fprintf(stderr, "%s: ", program_invocation_name);

    va_list args;
    int ret;
    va_start(args, message);
    ret = vfprintf(stderr, message, args);
    va_end(args);

    if (errnum) {
        fprintf(stderr, ": %s", strerror(errnum));
    }

    fprintf(stderr, "\n" );

    if (status) {
        exit(status);
    }
}
