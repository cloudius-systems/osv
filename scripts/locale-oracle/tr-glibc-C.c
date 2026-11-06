/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * Record what glibc's C locale writes on a wide stream for each wide
 * character 0x80..0x10ffff. Prints "<cp> <bytes>" (hex) for every code point
 * that does not write exactly "?", then "ferror=<n>":
 *
 *   cc -O2 -o tr-glibc-C tr-glibc-C.c && ./tr-glibc-C > tr-glibc-C.txt
 */
#include <locale.h>
#include <stdio.h>
#include <wchar.h>
#include <unistd.h>

int main(void)
{
    setlocale(LC_ALL, "C");
    FILE *f = tmpfile();
    long pos = 0;
    unsigned char b[64];
    for (unsigned cp = 0x80; cp <= 0x10ffff; cp++) {
        fputwc(cp, f);
        fflush(f);
        long n = ftell(f);
        ssize_t r = pread(fileno(f), b, n - pos, pos);
        if (!(r == 1 && b[0] == '?')) {
            printf("%x ", cp);
            for (ssize_t i = 0; i < r; i++) {
                printf("%02x", b[i]);
            }
            printf("\n");
        }
        pos = n;
    }
    printf("ferror=%d\n", ferror(f));
    return 0;
}
