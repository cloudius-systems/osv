/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Probe against glibc: every string of up to <maxlen> tokens over the alphabet given as hex
 * arguments; prints each pair (a < b in enumeration order) where sign(strcoll) differs from
 * sign(strcmp) of the strxfrm outputs. enum <locale> <maxlen> <hex>... */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int sg(int x) { return (x > 0) - (x < 0); }
static char tok[16][16];
static char str[20000][64], xf[20000][512];
static int ntok, nstr;
static void gen(char *cur, int depth, int maxlen)
{
    strcpy(str[nstr++], cur);
    if (depth == maxlen) return;
    size_t l = strlen(cur);
    for (int i = 0; i < ntok; i++) { strcpy(cur + l, tok[i]); gen(cur, depth + 1, maxlen); cur[l] = 0; }
}
int main(int argc, char **argv)
{
    if (argc < 4 || !setlocale(LC_ALL, argv[1])) return 1;
    int maxlen = atoi(argv[2]);
    for (int i = 3; i < argc; i++) {
        size_t k = 0; unsigned v;
        for (const char *p = argv[i]; p[0] && p[1]; p += 2) { sscanf(p, "%2x", &v); tok[ntok][k++] = v; }
        tok[ntok++][k] = 0;
    }
    char cur[64] = "";
    gen(cur, 0, maxlen);
    for (int i = 0; i < nstr; i++) strxfrm(xf[i], str[i], 512);
    long bad = 0;
    for (int i = 0; i < nstr; i++)
        for (int j = i + 1; j < nstr; j++) {
            int c = sg(strcoll(str[i], str[j])), d = sg(strcmp(xf[i], xf[j]));
            if (c != d) {
                bad++;
                printf("c=%d x=%d ", c, d);
                for (char *p = str[i]; *p; p++) printf("%02x", (unsigned char)*p);
                printf(" ");
                for (char *p = str[j]; *p; p++) printf("%02x", (unsigned char)*p);
                puts("");
            }
        }
    fprintf(stderr, "strings %d mismatches %ld\n", nstr, bad);
    return 0;
}
