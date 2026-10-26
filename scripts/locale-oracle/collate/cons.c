/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Probe against glibc: is strcoll consistent with strcmp of strxfrm over random pairs
 * (cons <locale> <seed> <pairs>), and what strxfrm/wcsxfrm write and return for short n. */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
static int sg(int x) { return (x > 0) - (x < 0); }
int main(int argc, char **argv)
{
    if (argc < 4 || !setlocale(LC_ALL, argv[1])) { puts("nolocale"); return 1; }
    const char *al[] = {"a", "b", "A", "B", "e", "\xc3\xa9", "\xc3\xa8", "E", "-", " ", "1", "2", "$", "c",
                        "h", "C", "H", "\xcc\x81", "\xe4\xb8\x80", "\x80", "\xff", "\xc2", "\x01", "L",
                        "\xc2\xb7", "\xd0\xb0", "'"};
    int na = sizeof al / sizeof *al;
    srand(atoi(argv[2]));
    long bad = 0, eq = 0, n = atol(argv[3]);
    for (long it = 0; it < n; it++) {
        char s[2][64];
        for (int k = 0; k < 2; k++) {
            s[k][0] = 0;
            int L = rand() % 7;
            for (int j = 0; j < L; j++) strcat(s[k], al[rand() % na]);
        }
        char x[2][1024];
        strxfrm(x[0], s[0], 1024);
        strxfrm(x[1], s[1], 1024);
        int c = sg(strcoll(s[0], s[1])), d = sg(strcmp(x[0], x[1]));
        if (!c) eq++;
        if (c != d) {
            bad++;
            if (bad < 10) {
                printf("INCONS c=%d d=%d ", c, d);
                for (int k = 0; k < 2; k++) { for (char *p = s[k]; *p; p++) printf("%02x", (unsigned char)*p); printf(" "); }
                puts("");
            }
        }
    }
    printf("pairs %ld inconsistent %ld equal %ld\n", n, bad, eq);
    if (argc > 4) {
        char b[32];
        for (int k = 0; k < 20; k++) {
            memset(b, 0xaa, 32);
            size_t r = strxfrm(b, "ab", k);
            printf("n=%d r=%zu ", k, r);
            for (int i = 0; i < 20; i++) printf("%02x", (unsigned char)b[i]);
            puts("");
        }
        wchar_t w[32];
        for (int k = 0; k < 15; k++) {
            wmemset(w, 0xaaaa, 32);
            size_t r = wcsxfrm(w, L"ab", k);
            printf("wn=%d r=%zu ", k, r);
            for (int i = 0; i < 15; i++) printf("%x,", (unsigned)w[i]);
            puts("");
        }
    }
    return 0;
}
