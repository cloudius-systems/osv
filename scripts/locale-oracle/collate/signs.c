/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Probe against glibc: every string of up to <maxlen> tokens of the alphabet (hex arguments), one per
 * line as "s <hex>", then for each i < j a line "<i> <j> <strcoll sign> <wcscoll sign>". Model input.
 * signs <locale> <maxlen> <hex>... */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
static int sg(int x) { return (x > 0) - (x < 0); }
static char tok[16][16];
static char str[40000][64];
static wchar_t wstr[40000][64];
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
        for (const char *q = argv[i]; q[0] && q[1]; q += 2) { sscanf(q, "%2x", &v); tok[ntok][k++] = v; }
        tok[ntok++][k] = 0;
    }
    char cur[64] = "";
    gen(cur, 0, maxlen);
    for (int i = 0; i < nstr; i++) {
        mbstowcs(wstr[i], str[i], 64);
        printf("s ");
        for (char *q = str[i]; *q; q++) printf("%02x", (unsigned char)*q);
        puts("");
    }
    for (int i = 0; i < nstr; i++)
        for (int j = i + 1; j < nstr; j++)
            printf("%d %d %d %d\n", i, j, sg(strcoll(str[i], str[j])), sg(wcscoll(wstr[i], wstr[j])));
    return 0;
}
