/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stdio.h>

int main(int argc, char *argv[])
{
    for (int y = 0; y < 1000; y++) {
        for (int x = 0; x < 60; x++) {
            printf("%c", '0' + x + y % 20);
        }
        printf("\n");
    }
    return 0;
}
