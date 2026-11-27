/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Probe against glibc: short random strings over a small alphabet where strcoll's sign differs
 * from strcmp of the strxfrm outputs; prints the shortest found (minpairs <locale> <seed> <n>). */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int sg(int x) { return (x > 0) - (x < 0); }
int main(int argc, char **argv)
{
    if (argc < 4 || !setlocale(LC_ALL, argv[1])) return 1;
    const char *al[] = {"a", "c", " ", "-", "\xcc\x81", "\xcc\x80", "\xc3\xa9", "e", "\xe4\xb8\x80", "\xc2\xb7", "1", "A"};
    int na = sizeof al / sizeof *al;
    srand(atoi(argv[2]));
    long n = atol(argv[3]);
    int best = 99;
    for (long it = 0; it < n; it++) {
        char s[2][64]; int len = 0;
        for (int k = 0; k < 2; k++) {
            s[k][0] = 0;
            int L = rand() % 4;
            for (int j = 0; j < L; j++) strcat(s[k], al[rand() % na]);
            len += L;
        }
        char x[2][512];
        strxfrm(x[0], s[0], 512);
        strxfrm(x[1], s[1], 512);
        if (sg(strcoll(s[0], s[1])) != sg(strcmp(x[0], x[1])) && len <= best) {
            best = len;
            printf("%d c=%d ", len, sg(strcoll(s[0], s[1])));
            for (int k = 0; k < 2; k++) { printf("["); for (char *p = s[k]; *p; p++) printf("%02x", (unsigned char)*p); printf("] "); }
            puts("");
        }
    }
    return 0;
}
