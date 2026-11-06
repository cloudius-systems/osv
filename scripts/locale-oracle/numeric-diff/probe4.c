/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* glibc probe: which grouped inputs scanf's ' flag accepts, and what value it
 * takes.  probe4 <locale> <input>...  (',' in an input is replaced by the
 * locale's thousands separator and '.' by its decimal point, so one input
 * list serves every locale; '_' stands for a literal ',' and ':' for '.') */
#define _GNU_SOURCE
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
int main(int argc, char **argv)
{
    if (!setlocale(LC_ALL, argv[1])) return 1;
    struct lconv *lc = localeconv();
    printf("== %s\n", argv[1]);
    for (int i = 2; i < argc; i++) {
        char s[256] = "";
        for (const char *p = argv[i]; *p; p++) {
            if (*p == ',') strcat(s, lc->thousands_sep);
            else if (*p == '.') strcat(s, lc->decimal_point);
            else if (*p == '_') strcat(s, ",");
            else if (*p == ':') strcat(s, ".");
            else strncat(s, p, 1);
        }
        double d = -1; int n = -1, r = sscanf(s, "%'lf%n", &d, &n);
        long li = -1; int ni = -1, ri = sscanf(s, "%'ld%n", &li, &ni);
        double d5 = -1; int n5 = -1, r5 = sscanf(s, "%'5lf%n", &d5, &n5);
        wchar_t w[256]; mbstowcs(w, s, 256);
        double wd = -1; int wn = -1, wr = swscanf(w, L"%'lf%n", &wd, &wn);
        long wl = -1; int wni = -1, wri = swscanf(w, L"%'ld%n", &wl, &wni);
        char *e; double sd = strtod(s, &e);
        printf("  %-14s 'lf %d %.17g +%d | 'ld %d %ld +%d | '5lf %d %.17g +%d | w'lf %d %.17g +%d | w'ld %d %ld +%d | strtod %.17g +%d\n",
               argv[i], r, d, n, ri, li, ni, r5, d5, n5, wr, wd, wn, wri, wl, wni, sd, (int)(e - s));
    }
}
