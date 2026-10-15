/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Probe against glibc. Reads lines from stdin:
 *   x <hex>          strxfrm and wcsxfrm of the string: "x <hex>|<n>|<bytes>|<wn>|<words,>|<mbstowcs,>"
 *   c <hex> <hex>    sign of strcoll and wcscoll: "c <hex> <hex> <s> <w> <wa,> <wb,>" (w is "-" if mbstowcs fails)
 * xf <locale> */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define SMAX (1 << 18)
static size_t unhex(const char *p, char *s)
{
    size_t k = 0;
    unsigned v;
    while (k < SMAX - 1 && p[0] && p[1] && p[0] != ' ' && p[0] != '\n' && sscanf(p, "%2x", &v) == 1) { s[k++] = v; p += 2; }
    s[k] = 0;
    return k;
}
static int sg(int x) { return (x > 0) - (x < 0); }
static char s[SMAX], t[SMAX], buf[8 * SMAX], line[4 * SMAX + 16];
static wchar_t ws[SMAX], wt[SMAX], wb[8 * SMAX];
int main(int argc, char **argv)
{
    if (argc < 2 || !setlocale(LC_ALL, argv[1])) { puts("nolocale"); return 1; }
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == 'x') {
            size_t k = unhex(line + 2, s);
            size_t n = strxfrm(buf, s, sizeof buf);
            printf("x %s|%zu|", line + 2, n);
            for (size_t i = 0; i < n; i++) printf("%02x", (unsigned char)buf[i]);
            size_t m = mbstowcs(ws, s, SMAX);
            if (m != (size_t)-1) {
                size_t wn = wcsxfrm(wb, ws, 8 * SMAX);
                printf("|%zu|", wn);
                for (size_t i = 0; i < wn; i++) printf("%x,", (unsigned)wb[i]);
                printf("|");
                for (size_t i = 0; i < m; i++) printf("%x,", (unsigned)ws[i]);
            } else printf("|-|");
            puts("");
            (void)k;
        } else if (line[0] == 'c') {
            char *sp = strchr(line + 2, ' ');
            if (!sp) continue;
            unhex(line + 2, s);
            unhex(sp + 1, t);
            int r = sg(strcoll(s, t));
            size_t a = mbstowcs(ws, s, SMAX), b = mbstowcs(wt, t, SMAX);
            if (a != (size_t)-1 && b != (size_t)-1) {
                printf("%s %d %d ", line, r, sg(wcscoll(ws, wt)));
                for (size_t i = 0; i < a; i++) printf("%x,", (unsigned)ws[i]);
                printf(" ");
                for (size_t i = 0; i < b; i++) printf("%x,", (unsigned)wt[i]);
                puts("");
            } else printf("%s %d -\n", line, r);
        }
    }
    return 0;
}
