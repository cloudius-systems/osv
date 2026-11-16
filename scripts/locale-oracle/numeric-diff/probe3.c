/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* glibc probe: strtod, wcstod and scanf with a locale's decimal point and
 * thousands separator.  probe3 <locale>... */
#define _GNU_SOURCE
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static const char *in[] = { "1.5", "1,5", "1.234,5", "1,234.5", "1.234.567,5", "12.34", "1,", "1.", ",5", ".5", "-,5",
                            " 1,5e3", "1,5e", "0x1,8p1", "0x1.8p1", "0x,8", "inf", "nan", "1e5,5", "1\xe2\x80\xaf" "234,5",
                            "1\xe2\x80\xaf" "234", "1\xe2\x80\xaf", "1\xd9\xab" "5", "\xd9\xab" "5", "1\xd9\xac" "234", "12,345,678.5",
                            "1.23.4", "1.2345", "123.456.789", ",", ".", "1,2,3", "1.000", "1.00", "0.000,5", "00.000" };
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!setlocale(LC_ALL, argv[i])) { printf("%s: no\n", argv[i]); continue; }
        printf("== %s\n", argv[i]);
        locale_t l = newlocale(LC_ALL_MASK, argv[i], 0);
        for (unsigned k = 0; k < sizeof in / sizeof *in; k++) {
            char *e; double d = strtod(in[k], &e);
            char *e2; double d2 = strtod_l(in[k], &e2, l);
            wchar_t w[64]; mbstowcs(w, in[k], 64); wchar_t *we; double d3 = wcstod(w, &we);
            double s1 = -1, s2 = -1; int n1 = -1, n2 = -1, r1, r2;
            r1 = sscanf(in[k], "%lf%n", &s1, &n1);
            r2 = sscanf(in[k], "%'lf%n", &s2, &n2);
            float s3 = -1; int n3 = -1; int r3 = sscanf(in[k], "%3f%n", &s3, &n3);
            int i1 = -1, n4 = -1, r4 = sscanf(in[k], "%'d%n", &i1, &n4);
            double s5 = -1; int n5 = -1; int r5 = swscanf(w, L"%'lf%n", &s5, &n5);
            printf("  %-16s strtod %.17g +%d  _l %.17g +%d  wcs %.17g +%d | scanf %d %.17g +%d  ' %d %.17g +%d  %%3f %d %g +%d  'd %d %d +%d  w' %d %.17g +%d\n",
                   in[k], d, (int)(e - in[k]), d2, (int)(e2 - in[k]), d3, (int)(we - w), r1, s1, n1, r2, s2, n2, r3, s3, n3, r4, i1, n4, r5, s5, n5);
        }
        freelocale(l);
    }
}
