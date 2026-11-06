/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* glibc probe: scanf flag order, the I flag on input, ' on i u x o, and
 * float syntax edge cases in scanf and strtod.  probe7 <locale>... */
#define _GNU_SOURCE
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
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
    for (int a = 1; a < argc; a++) {
        if (!setlocale(LC_ALL, argv[a])) continue;
        printf("== %s\n", argv[a]);
        struct lconv *lc = localeconv();
        char g[64]; snprintf(g, 64, "1%s234", lc->thousands_sep);
        static const char *fmts[] = { "%'d%n", "%*'d%n", "%'*d%n", "%1$'d%n", "%'i%n", "%'u%n", "%'x%n", "%'o%n", "%I'd%n", "%'Id%n", "%Id%n", "%'10d%n" };
        for (unsigned k = 0; k < sizeof fmts / sizeof *fmts; k++) {
            int v = -7, n = -7, r;
            if (!strcmp(fmts[k], "%1$'d%n")) r = sscanf(g, "%1$'d%2$n", &v, &n);
            else if (strstr(fmts[k], "*")) r = sscanf(g, fmts[k], &n);
            else r = sscanf(g, fmts[k], &v, &n);
            printf("  %-8s on ", fmts[k]); show(g); printf(" -> r=%d v=%d n=%d\n", r, v, n);
        }
        char od[64]; snprintf(od, 64, "%Id", 1234);
        int v = -7, n = -7, r = sscanf(od, "%Id%n", &v, &n);
        printf("  %%Id on "); show(od); printf(" -> r=%d v=%d n=%d\n", r, v, n);
        double dd = -7; r = sscanf(od, "%Ilf%n", &dd, &n); printf("  %%Ilf -> r=%d %g n=%d\n", r, dd, n);
        static const char *in[] = { "0x1.234", "01.234", "0x1,234", "1.5e", "1.5e+", "1e", "1ex", "0x", "0x1p", "0x1p+", "0xg", "nan(", "nan(12", "nan(1)", "infin", "infinity", "-", "+", ".e1", "1.5E-3x", "0x.p1", "0x1.p1", "1e99999", "1e-99999", "0x1p99999", "  \t12" };
        for (unsigned k = 0; k < sizeof in / sizeof *in; k++) {
            char s[64]; snprintf(s, 64, "%s", in[k]);
            for (char *p = s; *p; p++) if (*p == '.') { memmove(p + strlen(lc->decimal_point), p + 1, strlen(p)); memcpy(p, lc->decimal_point, strlen(lc->decimal_point)); p += strlen(lc->decimal_point) - 1; }
            double d = -7; int n = -7; int r = sscanf(s, "%lf%n", &d, &n);
            double d2 = -7; int n2 = -7; int r2 = sscanf(s, "%'lf%n", &d2, &n2);
            int i3 = -7, n3 = -7; int r3 = sscanf(s, "%'i%n", &i3, &n3);
            char *e; double sd = strtod(s, &e);
            printf("  %-10s lf %d %g +%d  'lf %d %g +%d  'i %d %d +%d  strtod %g +%d\n", in[k], r, d, n, r2, d2, n2, r3, i3, n3, sd, (int)(e - s));
        }
    }
}
