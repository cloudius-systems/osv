/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Probe against glibc: is strcoll a total preorder over every string of up to <maxlen> tokens of
 * the given alphabet? Counts antisymmetry failures (strcoll(a,b) != -strcoll(b,a)) and, after a
 * merge sort with strcoll, pairs i < j with strcoll(s_i, s_j) > 0 (transitivity failures).
 * order <locale> <maxlen> <hex>... */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int sg(int x) { return (x > 0) - (x < 0); }
static char tok[16][16];
static char str[40000][64];
static char *p[40000];
static int ntok, nstr;
static void gen(char *cur, int depth, int maxlen)
{
    strcpy(str[nstr++], cur);
    if (depth == maxlen) return;
    size_t l = strlen(cur);
    for (int i = 0; i < ntok; i++) { strcpy(cur + l, tok[i]); gen(cur, depth + 1, maxlen); cur[l] = 0; }
}
static int cmp(const void *a, const void *b) { return strcoll(*(char **)a, *(char **)b); }
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
    long anti = 0, trans = 0;
    for (int i = 0; i < nstr; i++) p[i] = str[i];
    for (int i = 0; i < nstr; i++)
        for (int j = i + 1; j < nstr; j++)
            if (sg(strcoll(str[i], str[j])) != -sg(strcoll(str[j], str[i]))) anti++;
    qsort(p, nstr, sizeof *p, cmp);
    for (int i = 0; i < nstr; i++)
        for (int j = i + 1; j < nstr; j++)
            if (strcoll(p[i], p[j]) > 0) {
                if (trans < 5) {
                    printf("order-violation ");
                    for (char *q = p[i]; *q; q++) printf("%02x", (unsigned char)*q);
                    printf(" > ");
                    for (char *q = p[j]; *q; q++) printf("%02x", (unsigned char)*q);
                    puts("");
                }
                trans++;
            }
    printf("strings %d antisymmetry-failures %ld order-violations %ld\n", nstr, anti, trans);
    return 0;
}
