/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Probe against glibc: how strxfrm/wcsxfrm encode a large "position" count (k ignorable characters
 * before a letter at a position level). big <locale> <ignorable hex> <letter hex> <k>... */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
int main(int argc, char **argv)
{
    if (argc < 5 || !setlocale(LC_ALL, argv[1])) return 1;
    char ign[16], let[16];
    size_t li = 0, ll = 0; unsigned v;
    for (const char *q = argv[2]; q[0] && q[1]; q += 2) { sscanf(q, "%2x", &v); ign[li++] = v; }
    for (const char *q = argv[3]; q[0] && q[1]; q += 2) { sscanf(q, "%2x", &v); let[ll++] = v; }
    for (int a = 4; a < argc; a++) {
        long k = atol(argv[a]);
        char *s = malloc(k * li + ll + 1), *p = s;
        for (long i = 0; i < k; i++) { memcpy(p, ign, li); p += li; }
        memcpy(p, let, ll); p[ll] = 0;
        size_t n = strxfrm(NULL, s, 0);
        char *x = malloc(n + 1);
        strxfrm(x, s, n + 1);
        printf("k=%ld n=%zu tail=", k, n);
        for (size_t i = n > 12 ? n - 12 : 0; i < n; i++) printf("%02x", (unsigned char)x[i]);
        wchar_t *w = malloc((k + ll + 1) * sizeof(wchar_t));
        mbstowcs(w, s, k + ll + 1);
        size_t wn = wcsxfrm(NULL, w, 0);
        wchar_t *wx = malloc((wn + 1) * sizeof(wchar_t));
        wcsxfrm(wx, w, wn + 1);
        printf(" wn=%zu wtail=", wn);
        for (size_t i = wn > 4 ? wn - 4 : 0; i < wn; i++) printf("%x,", (unsigned)wx[i]);
        puts("");
        free(s); free(x); free(w); free(wx);
    }
    return 0;
}
