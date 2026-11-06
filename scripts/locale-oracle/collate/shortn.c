/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Probe against glibc: what strxfrm writes into a buffer of n bytes (0xaa sentinel) for n = 0 ..
 * needed + 2, and the same for wcsxfrm; and wcscoll/wcsxfrm on wide values no mbstowcs produces.
 * shortn <locale> <hex>... */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
int main(int argc, char **argv)
{
    if (argc < 2 || !setlocale(LC_ALL, argv[1])) return 1;
    for (int a = 2; a < argc; a++) {
        char s[256]; size_t k = 0; unsigned v;
        for (const char *q = argv[a]; q[0] && q[1]; q += 2) { sscanf(q, "%2x", &v); s[k++] = v; }
        s[k] = 0;
        size_t need = strxfrm(NULL, s, 0);
        for (size_t n = 0; n <= need + 2; n++) {
            char b[512];
            memset(b, 0xaa, sizeof b);
            size_t r = strxfrm(b, s, n);
            size_t w = 0;
            while (w < sizeof b && (unsigned char)b[w] != 0xaa) w++;
            printf("%s n=%zu r=%zu wrote=%zu:", argv[a], n, r, w);
            for (size_t i = 0; i < w; i++) printf("%02x", (unsigned char)b[i]);
            puts("");
        }
        wchar_t ws[256], wb[512];
        if (mbstowcs(ws, s, 256) == (size_t)-1) continue;
        need = wcsxfrm(NULL, ws, 0);
        for (size_t n = 0; n <= need + 2; n++) {
            wmemset(wb, 0xaaaa, 512);
            size_t r = wcsxfrm(wb, ws, n);
            size_t w = 0;
            while (w < 512 && wb[w] != 0xaaaa) w++;
            printf("w%s n=%zu r=%zu wrote=%zu:", argv[a], n, r, w);
            for (size_t i = 0; i < w; i++) printf("%x,", (unsigned)wb[i]);
            puts("");
        }
    }
    wchar_t odd[] = {0xd800, 0xdfff, 0xfffe, 0x110000, 0x7fffffff, (wchar_t)0x80000000, (wchar_t)0xffffffff, 0x1fffff, 0xe0100, 0x10fffd, 0x61};
    for (size_t i = 0; i < sizeof odd / sizeof *odd; i++) {
        wchar_t w[3] = {odd[i], 0x61, 0};
        wchar_t x[64];
        size_t r = wcsxfrm(x, w, 64);
        printf("wide %x+a r=%zu:", (unsigned)odd[i], r);
        for (size_t j = 0; j < r && j < 64; j++) printf("%x,", (unsigned)x[j]);
        wchar_t one[2] = {odd[i], 0}, a1[2] = {0x1, 0}, ea[2] = {0xfffd, 0};
        printf(" coll-vs-U+0001=%d coll-vs-U+FFFD=%d\n", wcscoll(one, a1), wcscoll(one, ea));
    }
    return 0;
}
