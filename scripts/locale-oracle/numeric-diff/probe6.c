/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* glibc probe: integer edge cases with ' and I, and scanf field widths
 * across a multibyte separator or decimal point.  probe6 <locale>... */
#define _GNU_SOURCE
#include <locale.h>
#include <stdio.h>
#include <string.h>
static void show(const char *s)
{
    putchar('[');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p < 32 || *p > 126) printf("\\x%02x", *p); else putchar(*p);
    putchar(']');
}
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!setlocale(LC_ALL, argv[i])) continue;
        printf("== %s\n", argv[i]);
        char b[100];
        static const char *fi[] = { "%'#o", "%'#.10o", "%I.5d", "%I05d", "%I5d", "%'.0d", "%'#x", "%I#o", "%'I.3d", "%I.0d", "%'d", "%'.1d" };
        static const unsigned iv[] = { 0, 0123456, 1, 12345678 };
        for (unsigned k = 0; k < sizeof fi / sizeof *fi; k++) {
            printf("  %-8s", fi[k]);
            for (unsigned j = 0; j < 4; j++) { snprintf(b, sizeof b, fi[k], iv[j]); printf(" "); show(b); }
            printf("\n");
        }
        struct lconv *lc = localeconv();
        char in[4][64];
        snprintf(in[0], 64, "123%s456", lc->thousands_sep);
        snprintf(in[1], 64, "1234%s567", lc->thousands_sep);
        snprintf(in[2], 64, "1%s234%s567", lc->thousands_sep, lc->thousands_sep);
        snprintf(in[3], 64, "12%s5", lc->decimal_point);
        for (int k = 0; k < 4; k++) {
            printf("  in "); show(in[k]); printf("\n");
            for (int w = 1; w <= 9; w++) {
                char f1[16], f2[16], f3[16];
                snprintf(f1, 16, "%%'%dlf%%n", w); snprintf(f2, 16, "%%'%dld%%n", w); snprintf(f3, 16, "%%%dlf%%n", w);
                double d = -1, d3 = -1; long l = -1; int n = -1, n2 = -1, n3 = -1;
                int r = sscanf(in[k], f1, &d, &n), r2 = sscanf(in[k], f2, &l, &n2), r3 = sscanf(in[k], f3, &d3, &n3);
                printf("    w%d 'lf %d %g +%d  'ld %d %ld +%d  lf %d %g +%d\n", w, r, d, n, r2, l, n2, r3, d3, n3);
            }
        }
    }
}
