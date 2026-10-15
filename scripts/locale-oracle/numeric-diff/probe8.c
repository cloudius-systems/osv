/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* glibc probe: ' with a precision on o x X u d (with and without #), to see
 * how the precision zeros are counted.  probe8 <locale>... */
#define _GNU_SOURCE
#include <locale.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    static const unsigned v[] = { 7, 0123, 01234, 012345, 0123456, 01234567, 012345670, 0123456701u };
    static const char *f[] = { "%%'#.%do", "%%'.%do", "%%'#.%dx", "%%'.%dd" };
    for (int i = 1; i < argc; i++) {
        if (!setlocale(LC_ALL, argv[i])) continue;
        printf("== %s\n", argv[i]);
        for (unsigned k = 0; k < 4; k++)
            for (unsigned j = 0; j < sizeof v / sizeof *v; j++) {
                printf("  %-8s %011o:", f[k], v[j]);
                for (int p = 0; p <= 14; p += 2) {
                    char fmt[16], b[128], s[128] = "";
                    snprintf(fmt, sizeof fmt, f[k], p);
                    snprintf(b, sizeof b, fmt, v[j]);
                    int z = 0;
                    while (b[z] == '0') z++;
                    printf(" p%d:z%d/%zu[%s]", p, z, strlen(b), b);
                    (void)s;
                }
                printf("\n");
            }
    }
}
